#include "flexql.h"
#include <string>
#include <vector>
#include <cstring>
#include <iostream>
#include <unistd.h>
#include <arpa/inet.h>

/* ── DB HANDLE ─────────────────────────────────────────── */
struct flexql_db {
    int sock;
};

/* ── OPEN (CONNECT TO SERVER) ─────────────────────────── */
int flexql_open(const char* host, int port, flexql_db** db) {
    if (!db) return FLEXQL_ERROR;

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return FLEXQL_ERROR;

    sockaddr_in server{};
    server.sin_family = AF_INET;
    server.sin_port = htons(port);

    if (inet_pton(AF_INET, host, &server.sin_addr) <= 0)
        return FLEXQL_ERROR;

    if (connect(sock, (sockaddr*)&server, sizeof(server)) < 0)
        return FLEXQL_ERROR;

    *db = new flexql_db();
    (*db)->sock = sock;

    return FLEXQL_OK;
}

/* ── CLOSE ───────────────────────────────────────────── */
int flexql_close(flexql_db* db) {
    if (!db) return FLEXQL_ERROR;

    close(db->sock);
    delete db;
    return FLEXQL_OK;
}

/* ── EXEC ───────────────────────────────────────────── */
int flexql_exec(
    flexql_db* db,
    const char* sql,
    flexql_callback callback,
    void* arg,
    char** errmsg
) {
    if (!db || !sql) {
        if (errmsg) *errmsg = strdup("Invalid DB or SQL");
        return FLEXQL_ERROR;
    }

    std::string query = std::string(sql) + "\n";

    // Send query
    if (send(db->sock, query.c_str(), query.size(), 0) < 0) {
        if (errmsg) *errmsg = strdup("Send failed");
        return FLEXQL_ERROR;
    }

    // Receive response
    char buffer[4096];
    std::string response;

    int bytes;
    while ((bytes = recv(db->sock, buffer, sizeof(buffer)-1, 0)) > 0) {
        buffer[bytes] = '\0';
        response += buffer;

        if (response.find("END\n") != std::string::npos)
            break;
    }

    if (response.empty()) {
        if (errmsg) *errmsg = strdup("No response from server");
        return FLEXQL_ERROR;
    }

    /*
        Expected simple protocol:
        col1|col2|col3
        val1|val2|val3
        val1|val2|val3
        END
    */

    std::vector<std::string> lines;
    size_t pos = 0;

    while ((pos = response.find('\n')) != std::string::npos) {
        lines.push_back(response.substr(0, pos));
        response.erase(0, pos + 1);
    }

    if (lines.empty()) return FLEXQL_OK;

    // Check for error response
    if (lines[0].find("ERROR|") == 0) {
        std::string errText = lines[0].substr(6);  // Skip "ERROR|"
        if (errmsg) {
            *errmsg = strdup(errText.c_str());
        }
        return FLEXQL_ERROR;
    }

    // First line = columns
    std::vector<std::string> columns;
    {
        std::string line = lines[0];
        size_t p = 0;
        while ((p = line.find('|')) != std::string::npos) {
            columns.push_back(line.substr(0, p));
            line.erase(0, p + 1);
        }
        columns.push_back(line);
    }

    // Process rows
    for (size_t i = 1; i < lines.size(); i++) {
        if (lines[i] == "END") break;

        std::vector<std::string> values;
        std::string line = lines[i];

        size_t p = 0;
        while ((p = line.find('|')) != std::string::npos) {
            values.push_back(line.substr(0, p));
            line.erase(0, p + 1);
        }
        values.push_back(line);

        if (callback) {
            std::vector<char*> cVals;
            std::vector<char*> cCols;

            for (auto &v : values)
                cVals.push_back(const_cast<char*>(v.c_str()));

            for (auto &c : columns)
                cCols.push_back(const_cast<char*>(c.c_str()));

            callback(arg, cVals.size(), cVals.data(), cCols.data());
        }
    }

    return FLEXQL_OK;
}

/* ── FREE ───────────────────────────────────────────── */
void flexql_free(void* ptr) {
    free(ptr);
}