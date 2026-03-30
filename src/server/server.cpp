#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <string>
#include <vector>
#include <sstream>
#include <cstring>
#include <algorithm>
#include <thread>
#include <memory>
#include <cstdlib>
#include "../include/storage.hpp"
#include "../include/parser.hpp"

/* ── Global storage — initialised in main() ─────────────── */
static std::unique_ptr<StorageEngine> g_storage;

/* ── Helpers ─────────────────────────────────────────────── */
static std::string toUpper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), ::toupper);
    return s;
}

static bool compareValues(const std::string &a, const std::string &b, bool desc) {
    bool numA = false, numB = false;
    double na = 0, nb = 0;
    try { na = std::stod(a); numA = true; } catch (...) {}
    try { nb = std::stod(b); numB = true; } catch (...) {}
    if (numA && numB) return desc ? (na > nb) : (na < nb);
    return desc ? (a > b) : (a < b);
}

/* ── PROCESS QUERY ─────────────────────────────────────── */
static bool process_query(const std::string &query, ResultSet &result,
                           std::string &err) {
    ParsedQuery pq = Parser::parse(query);

    if (pq.type == QueryType::UNKNOWN) {
        err = pq.errorMsg.empty() ? "Invalid SQL" : pq.errorMsg;
        return false;
    }

    if (pq.type == QueryType::CREATE_TABLE) {
        Schema schema;
        schema.tableName = pq.tableName;
        schema.columns   = pq.colDefs;
        for (int i = 0; i < (int)pq.colDefs.size(); ++i) {
            if (pq.colDefs[i].primaryKey) { schema.pkIndex = i; break; }
        }
        return g_storage->createTable(schema, pq.ifNotExists, err);
    }

    if (pq.type == QueryType::SELECT || pq.type == QueryType::INSERT ||
        pq.type == QueryType::DELETE) {
        if (pq.tableName.empty()) { err = "Missing table name"; return false; }
        if (!g_storage->getTable(pq.tableName)) {
            err = "No such table: " + pq.tableName;
            return false;
        }
    }

    switch (pq.type) {

        case QueryType::DELETE:
            return g_storage->deleteRows(pq.tableName, err);

        case QueryType::INSERT: {
            if (pq.batchValues.empty()) {
                err = "No values provided for INSERT";
                return false;
            }
            for (const auto &rowVals : pq.batchValues) {
                std::vector<Value> vals;
                for (const auto &s : rowVals) {
                    if (s == "NULL") {
                        vals.push_back(std::monostate{});
                    } else {
                        bool numeric = false;
                        try {
                            size_t pos;
                            double d = std::stod(s, &pos);
                            if (pos == s.size()) { vals.push_back(d); numeric = true; }
                        } catch (...) {}
                        if (!numeric) vals.push_back(s);
                    }
                }
                if (!g_storage->insertRow(pq.tableName, vals, pq.expiresAt, err))
                    return false;
            }
            return true;
        }

        case QueryType::SELECT: {
            bool ok;
            std::vector<std::string> originalSelectCols = pq.selectCols;

            bool orderByNotInSelect = !pq.orderByCol.empty() && !pq.selectAll &&
                std::find(pq.selectCols.begin(), pq.selectCols.end(),
                          pq.orderByCol) == pq.selectCols.end();

            auto applyOrderBy = [&](ResultSet &rs) {
                if (pq.orderByCol.empty() || rs.rows.empty()) return;
                int si = -1;
                for (size_t i = 0; i < rs.columns.size(); ++i)
                    if (toUpper(rs.columns[i]) == pq.orderByCol) { si = (int)i; break; }
                if (si < 0) return;
                std::sort(rs.rows.begin(), rs.rows.end(),
                    [si, &pq](const Row &a, const Row &b) {
                        return compareValues(valueToString(a.cells[si]),
                                             valueToString(b.cells[si]),
                                             pq.orderByDesc);
                    });
            };

            if (pq.hasJoin) {
                if (!g_storage->getTable(pq.joinTable)) {
                    err = "Join table does not exist: " + pq.joinTable;
                    return false;
                }
                ok = g_storage->selectJoin(
                    pq.tableName, pq.joinTable,
                    pq.joinColA, pq.joinColB,
                    pq.selectCols, pq.selectAll,
                    pq.where.column, pq.where.op, pq.where.value,
                    pq.where.present, result, err);
                if (ok) applyOrderBy(result);

            } else if (orderByNotInSelect) {
                ResultSet tmp;
                ok = g_storage->selectRows(pq.tableName, {}, true,
                                            pq.where.column, pq.where.op,
                                            pq.where.value, pq.where.present,
                                            tmp, err);
                if (ok) {
                    applyOrderBy(tmp);
                    int si = -1;
                    for (size_t i = 0; i < tmp.columns.size(); ++i)
                        if (toUpper(tmp.columns[i]) == pq.orderByCol) { si=(int)i; break; }
                    (void)si;

                    std::vector<int> selIdxs;
                    for (const auto &col : originalSelectCols)
                        for (size_t i = 0; i < tmp.columns.size(); ++i)
                            if (toUpper(tmp.columns[i]) == toUpper(col))
                                { selIdxs.push_back((int)i); break; }

                    result.columns = originalSelectCols;
                    for (const auto &row : tmp.rows) {
                        Row nr;
                        for (int idx : selIdxs) nr.cells.push_back(row.cells[idx]);
                        result.rows.push_back(nr);
                    }
                }

            } else {
                ok = g_storage->selectRows(pq.tableName, pq.selectCols, pq.selectAll,
                                            pq.where.column, pq.where.op, pq.where.value,
                                            pq.where.present, result, err);
                if (ok) applyOrderBy(result);
            }

            return ok;
        }

        default:
            err = "Unsupported query type";
            return false;
    }
}

/* ── SEND RESULT ────────────────────────────────────────── */
/*
 * Wire format (tab-delimited — values containing spaces are safe):
 *
 *   HEADER\tcol1\tcol2\n
 *   ROW\tval1\tval2\n
 *   END\n
 *
 *   ERR\t<message>\n   (on failure)
 */
static void send_result(int fd, const ResultSet &result) {
    std::ostringstream out;
    out << "HEADER";
    for (const auto &col : result.columns) out << "\t" << col;
    out << "\n";
    for (const auto &row : result.rows) {
        out << "ROW";
        for (const auto &cell : row.cells) out << "\t" << valueToString(cell);
        out << "\n";
    }
    out << "END\n";
    std::string resp = out.str();
    send(fd, resp.c_str(), resp.size(), MSG_NOSIGNAL);
}

static void send_error(int fd, const std::string &msg) {
    std::string resp = "ERR\t" + msg + "\n";
    send(fd, resp.c_str(), resp.size(), MSG_NOSIGNAL);
}

/* ── HANDLE CLIENT ──────────────────────────────────────── */
static void handle_client(int fd) {
    std::string buf;
    char chunk[4096];

    while (true) {
        ssize_t n = recv(fd, chunk, sizeof(chunk) - 1, 0);
        if (n <= 0) break;
        chunk[n] = '\0';
        buf += chunk;

        size_t pos;
        while ((pos = buf.find('\n')) != std::string::npos) {
            std::string query = buf.substr(0, pos);
            buf.erase(0, pos + 1);
            if (!query.empty() && query.back() == '\r') query.pop_back();
            if (query.empty()) continue;

            ResultSet result;
            std::string err;
            bool ok = process_query(query, result, err);

            if (!ok) send_error(fd, err);
            else     send_result(fd, result);
        }
    }

    close(fd);
}

/* ── START SERVER ───────────────────────────────────────── */
static void start_server(int port) {
    int sfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sfd < 0) { perror("socket"); return; }

    int opt = 1;
    setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(sfd, (sockaddr *)&addr, sizeof(addr)) < 0) { perror("bind"); return; }
    if (listen(sfd, 128) < 0)                           { perror("listen"); return; }

    std::cout << "FlexQL Server running on port " << port << std::endl;

    while (true) {
        int cfd = accept(sfd, nullptr, nullptr);
        if (cfd < 0) continue;
        std::thread([cfd]{ handle_client(cfd); }).detach();
    }
}

/* ── MAIN ───────────────────────────────────────────────── */
int main(int argc, char *argv[]) {
    int         port    = 9000;
    std::string dataDir = "flexql_data";
    if (argc >= 2) port    = std::atoi(argv[1]);
    if (argc >= 3) dataDir = argv[2];

    g_storage = std::make_unique<StorageEngine>(dataDir);
    start_server(port);
    return 0;
}
