#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <string>
#include <vector>
#include <sstream>
#include <cstring>
#include "../include/storage.hpp"
#include "../include/parser.hpp"

/* ── Global Storage Engine ─────────────────────────────── */
static StorageEngine storage;

/* ── PROCESS QUERY ─────────────────────────────────────── */
bool process_query(const std::string& query, ResultSet& result, std::string& err) {
    ParsedQuery pq = Parser::parse(query);

    if (pq.type == QueryType::UNKNOWN) {
        err = pq.errorMsg.empty() ? "Invalid SQL" : pq.errorMsg;
        return false;
    }

    // CREATE TABLE
    if (pq.type == QueryType::CREATE_TABLE) {
        Schema schema;
        schema.tableName = pq.tableName;
        schema.columns = pq.colDefs;
        for (int i = 0; i < (int)pq.colDefs.size(); ++i) {
            if (pq.colDefs[i].primaryKey) {
                schema.pkIndex = i;
                break;
            }
        }
        return storage.createTable(schema, pq.ifNotExists, err);
    }

    // For other commands, table must exist
    Table* t = storage.getTable(pq.tableName);
    if (!t) {
        err = "No such table: " + pq.tableName;
        return false;
    }

    switch (pq.type) {
        case QueryType::DELETE:
            return storage.deleteRows(pq.tableName, err);

        case QueryType::INSERT: {
            for (const auto& rowVals : pq.batchValues) {
                std::vector<Value> vals;
                for (const auto& s : rowVals) {
                    if (s == "NULL") vals.push_back(std::monostate{});
                    else vals.push_back(s);
                }
                if (!storage.insertRow(pq.tableName, vals, pq.expiresAt, err))
                    return false;
            }
            return true;
        }

        case QueryType::SELECT: {
            bool ok;
            if (pq.hasJoin) {
                ok = storage.selectJoin(pq.tableName, pq.joinTable,
                                        pq.joinColA, pq.joinColB,
                                        pq.selectCols, pq.selectAll,
                                        pq.where.column, pq.where.op, pq.where.value, pq.where.present,
                                        result, err);
            } else {
                ok = storage.selectRows(pq.tableName, pq.selectCols, pq.selectAll,
                                        pq.where.column, pq.where.op, pq.where.value, pq.where.present,
                                        result, err);
            }
            return ok;
        }

        default:
            err = "Unsupported query type";
            return false;
    }
}

/* ── SEND RESULT ───────────────────────────────────────── */
void send_result(int client_sock, const ResultSet& result) {
    std::ostringstream out;

    // Columns
    for (size_t i = 0; i < result.columns.size(); i++) {
        out << result.columns[i];
        if (i != result.columns.size() - 1) out << " ";
    }
    out << "\n";

    // Rows
    for (const auto& row : result.rows) {
        for (size_t i = 0; i < row.cells.size(); i++) {
            out << valueToString(row.cells[i]);
            if (i != row.cells.size() - 1) out << " ";
        }
        out << "\n";
    }

    out << "END\n";
    std::string response = out.str();
    send(client_sock, response.c_str(), response.size(), 0);
}

/* ── HANDLE CLIENT ─────────────────────────────────────── */
void handle_client(int client_sock) {
    std::string recvBuf;

    while (true) {
        char chunk[4096];
        ssize_t n = recv(client_sock, chunk, sizeof(chunk) - 1, 0);
        if (n <= 0) break; // client disconnected
        chunk[n] = '\0';
        recvBuf += chunk;

        size_t pos;
        while ((pos = recvBuf.find('\n')) != std::string::npos) {
            std::string query = recvBuf.substr(0, pos);
            recvBuf.erase(0, pos + 1);
            if (query.empty()) continue;

            ResultSet result;
            std::string err;
            bool ok = process_query(query, result, err);

            if (!ok) {
                std::string errResp = "ERROR|" + err + "\nEND\n";
                send(client_sock, errResp.c_str(), errResp.size(), 0);
                continue;
            }

            send_result(client_sock, result);
        }
    }

    close(client_sock);
}

/* ── START SERVER ─────────────────────────────────────── */
void start_server(int port) {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;

    bind(server_fd, (struct sockaddr*)&addr, sizeof(addr));
    listen(server_fd, 5);

    std::cout << "FlexQL Server running on port " << port << std::endl;

    while (true) {
        int client_sock = accept(server_fd, nullptr, nullptr);
        handle_client(client_sock); // single-threaded
    }
}

/* ── MAIN ─────────────────────────────────────────────── */
int main() {
    start_server(9000);
    return 0;
}