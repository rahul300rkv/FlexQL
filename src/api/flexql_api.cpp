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
    if (!host || !outDb) return 1;

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return 1;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons((uint16_t)port);
    if (inet_pton(AF_INET, host, &addr.sin_addr) <= 0) {
        close(sock); return 1;
    }

    if (connect(sock, (sockaddr *)&addr, sizeof(addr)) < 0) {
        close(sock); return 1;
    }

    FlexQL *db = (FlexQL *)malloc(sizeof(FlexQL));
    if (!db) { close(sock); return 1; }
    db->sock = sock;
    *outDb = db;
    return 0;
}

/* ── flexql_close ────────────────────────────────────────── */
int flexql_close(FlexQL *db) {
    if (!db) return 1;
    if (db->sock >= 0) {
        close(db->sock);
        db->sock = -1;
    }
    free(db);
    return 0;
}

/* ── flexql_exec ─────────────────────────────────────────── */
int flexql_exec(FlexQL *db,
                const char *sql,
                int (*callback)(void *, int, char **, char **),
                void *arg,
                char **errmsg) {

    if (!db || db->sock < 0 || !sql) {
        if (errmsg) *errmsg = strdup("Invalid database handle");
        return 1;
    }

    std::string query = sql;
    if (!query.empty() && query.back() != ';') query += ";";
    query += "\n";

    // Send query to server
    if (send(db->sock, query.c_str(), query.length(), 0) <= 0) {
        if (errmsg) *errmsg = strdup("Failed to send query to server");
        return 1;
    }

    // Read full response
    std::string response;
    char buf[4096];
    bool done = false;
    
    while (!done) {
        ssize_t n = recv(db->sock, buf, sizeof(buf) - 1, 0);
        if (n <= 0) break;
        buf[n] = '\0';
        response += buf;
        if (response.find("\nEND\n") != std::string::npos) {
            done = true;
        }
    }
    
    // DEBUG: Print the response
    std::cerr << "API: Response received: " << response << std::endl;
    
    // Check for error response
    if (response.find("ERROR|") != std::string::npos) {
        // Extract error message
        size_t start = response.find("ERROR|") + 6;
        size_t end = response.find("\n", start);
        if (end != std::string::npos && errmsg) {
            std::string errorMsg = response.substr(start, end - start);
            *errmsg = strdup(errorMsg.c_str());
        }
        std::cerr << "API: Returning ERROR (1)" << std::endl;
        return 1; // Error
    }
    
    std::cerr << "API: Returning SUCCESS (0)" << std::endl;
    
    // Parse successful response
    std::vector<std::string> lines;
    std::stringstream ss(response);
    std::string line;
    while (std::getline(ss, line)) {
        if (line == "END") break;
        lines.push_back(line);
    }
    
    if (lines.empty() || !callback) {
        return 0; // Success
    }
    
    // Parse headers
    std::vector<std::string> headers;
    std::stringstream headerStream(lines[0]);
    std::string header;
    while (headerStream >> header) {
        headers.push_back(header);
    }
    
    // Process each row
    for (size_t i = 1; i < lines.size(); i++) {
        std::vector<std::string> values;
        std::stringstream rowStream(lines[i]);
        std::string value;
        while (rowStream >> value) {
            values.push_back(value);
        }
        
        std::vector<char*> argv;
        std::vector<char*> colNames;
        
        for (auto& v : values) {
            argv.push_back(const_cast<char*>(v.c_str()));
        }
        for (auto& h : headers) {
            colNames.push_back(const_cast<char*>(h.c_str()));
        }
        
        int rc = callback(arg, (int)argv.size(), argv.data(), colNames.data());
        if (rc != 0) break;
    }
    
    return 0; // Success
}

/* ── flexql_free ─────────────────────────────────────────── */
void flexql_free(void *ptr) {
    free(ptr);
}