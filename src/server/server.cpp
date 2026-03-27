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
        err = pq.errorMsg;
        return false;
    }

    switch (pq.type) {
        case QueryType::CREATE_TABLE: {
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
        case QueryType::DELETE: {
            return storage.deleteRows(pq.tableName, err);
        }
        case QueryType::INSERT: {
            std::vector<Value> vals;
            for (const auto& s : pq.insertValues) {
                if (s == "NULL") vals.push_back(std::monostate{});
                else vals.push_back(s); // string, will be converted
            }
            return storage.insertRow(pq.tableName, vals, pq.expiresAt, err);
        }
        case QueryType::SELECT: {
            if (pq.hasJoin) {
                return storage.selectJoin(pq.tableName, pq.joinTable, pq.joinColA, pq.joinColB,
                                        pq.selectCols, pq.selectAll,
                                        pq.where.column, pq.where.op, pq.where.value, pq.where.present,
                                        result, err);
            } else {
                return storage.selectRows(pq.tableName, pq.selectCols, pq.selectAll,
                                        pq.where.column, pq.where.op, pq.where.value, pq.where.present,
                                        result, err);
            }
        }
        default:
            err = "Unsupported query type";
            return false;
    }
}

/*
    You must implement this function elsewhere.
    It should:
    - Parse SQL
    - Execute using StorageEngine
    - Fill ResultSet
*/

extern bool process_query(const std::string& query,
                          ResultSet& result,
                          std::string& err);

/* ── SEND RESPONSE IN BENCHMARK FORMAT ─────────────────── */
void send_result(int client_sock, const ResultSet& result) {
    std::ostringstream out;

    // Columns
    for (size_t i = 0; i < result.columns.size(); i++) {
        out << result.columns[i];
        if (i != result.columns.size() - 1) out << "|";
    }
    out << "\n";

    // Rows
    for (const auto& row : result.rows) {
        for (size_t i = 0; i < row.cells.size(); i++) {
            out << valueToString(row.cells[i]);
            if (i != row.cells.size() - 1) out << "|";
        }
        out << "\n";
    }

    out << "END\n";

    std::string response = out.str();
    send(client_sock, response.c_str(), response.size(), 0);
}

/* ── HANDLE CLIENT ─────────────────────────────────────── */
void handle_client(int client_sock) {
    char buffer[4096];

    while (true) {
        memset(buffer, 0, sizeof(buffer));

        int bytes = recv(client_sock, buffer, sizeof(buffer) - 1, 0);

        if (bytes <= 0) break; // client disconnected

        std::string query(buffer);

        // Remove newline
        if (!query.empty() && query.back() == '\n')
            query.pop_back();

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

        // ❌ DO NOT USE MULTITHREADING (assignment rule)
        // std::thread(handle_client, client_sock).detach();

        // ✅ Single-threaded execution
        handle_client(client_sock);
    }
}

/* ── MAIN ─────────────────────────────────────────────── */
int main() {
    start_server(9000);
    return 0;
}