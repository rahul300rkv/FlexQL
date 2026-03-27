#include "../../include/flexql.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <sstream>
#include <iostream>  // Add for debug

/* Internal handle */
struct FlexQL {
    int sock = -1;
};

/* ── flexql_open ─────────────────────────────────────────── */
int flexql_open(const char *host, int port, FlexQL **outDb) {
    if (!host || !outDb) return FLEXQL_ERROR;

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return FLEXQL_ERROR;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons((uint16_t)port);
    if (inet_pton(AF_INET, host, &addr.sin_addr) <= 0) {
        close(sock); return FLEXQL_ERROR;
    }

    if (connect(sock, (sockaddr *)&addr, sizeof(addr)) < 0) {
        close(sock); return FLEXQL_ERROR;
    }

    FlexQL *db = (FlexQL *)malloc(sizeof(FlexQL));
    if (!db) { close(sock); return FLEXQL_ERROR; }
    db->sock = sock;
    *outDb = db;
    return FLEXQL_OK;
}

/* ── flexql_close ────────────────────────────────────────── */
int flexql_close(FlexQL *db) {
    if (!db) return FLEXQL_ERROR;
    if (db->sock >= 0) {
        close(db->sock);
        db->sock = -1;
    }
    free(db);
    return FLEXQL_OK;
}

/* ── flexql_exec ─────────────────────────────────────────── */
int flexql_exec(FlexQL *db,
                const char *sql,
                int (*callback)(void *, int, char **, char **),
                void *arg,
                char **errmsg) {

    if (!db || db->sock < 0 || !sql) {
        if (errmsg) *errmsg = strdup("Invalid database handle");
        return FLEXQL_ERROR;
    }

    std::string query = sql;
    if (!query.empty() && query.back() != ';') query += ";";

    // Send query to server
    if (send(db->sock, query.c_str(), query.length(), 0) <= 0) {
        if (errmsg) *errmsg = strdup("Failed to send query to server");
        return FLEXQL_ERROR;
    }

    // Read the first line to check for error
    std::string firstLine;
    char c;
    while (read(db->sock, &c, 1) > 0) {
        if (c == '\n') break;
        firstLine += c;
    }
    
    std::cerr << "API: First line: [" << firstLine << "]" << std::endl;
    
    // If first line starts with ERROR|, return error
    if (firstLine.find("ERROR|") == 0) {
        std::cerr << "API: DETECTED ERROR!" << std::endl;
        
        // Extract error message
        if (errmsg) {
            std::string errorMsg = firstLine.substr(6);
            *errmsg = strdup(errorMsg.c_str());
            std::cerr << "API: Error message: " << errorMsg << std::endl;
        }
        
        // Read the rest of the response until END to clear the socket buffer
        std::string rest;
        char buf[1024];
        while (true) {
            ssize_t n = read(db->sock, buf, sizeof(buf) - 1);
            if (n <= 0) break;
            buf[n] = '\0';
            rest += buf;
            if (rest.find("END") != std::string::npos) break;
        }
        
        std::cerr << "API: Returning FLEXQL_ERROR (1)" << std::endl;
        return FLEXQL_ERROR;
    }
    
    std::cerr << "API: No error detected, processing normal response" << std::endl;
    
    // If no error, continue reading the full response
    std::string fullResponse = firstLine + "\n";
    char buf[8192];
    while (true) {
        ssize_t n = read(db->sock, buf, sizeof(buf) - 1);
        if (n <= 0) break;
        buf[n] = '\0';
        fullResponse += buf;
        if (fullResponse.find("\nEND\n") != std::string::npos) break;
    }
    
    // Parse the successful response
    std::vector<std::string> lines;
    std::stringstream ss(fullResponse);
    std::string line;
    while (std::getline(ss, line)) {
        if (line == "END") break;
        lines.push_back(line);
    }
    
    if (lines.empty()) {
        return FLEXQL_OK;
    }
    
    // Parse column headers (first line)
    std::vector<std::string> headers;
    std::stringstream headerStream(lines[0]);
    std::string header;
    while (headerStream >> header) {
        headers.push_back(header);
    }
    
    // Parse data rows (remaining lines)
    std::vector<std::vector<std::string>> rows;
    for (size_t i = 1; i < lines.size(); i++) {
        std::vector<std::string> row;
        std::stringstream rowStream(lines[i]);
        std::string value;
        while (rowStream >> value) {
            row.push_back(value);
        }
        rows.push_back(row);
    }
    
    // Call callback for each row if provided
    if (callback) {
        for (const auto& row : rows) {
            std::vector<char*> argv;
            for (const auto& val : row) {
                argv.push_back(const_cast<char*>(val.c_str()));
            }
            
            std::vector<char*> colNames;
            for (const auto& h : headers) {
                colNames.push_back(const_cast<char*>(h.c_str()));
            }
            
            callback(arg, (int)argv.size(), argv.data(), colNames.data());
        }
    }
    
    return FLEXQL_OK;
}

/* ── flexql_free ─────────────────────────────────────────── */
void flexql_free(void *ptr) {
    free(ptr);
}