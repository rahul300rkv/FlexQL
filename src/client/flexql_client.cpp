/*
 * FlexQL Client Library
 * Implements the public API: flexql_open, flexql_close, flexql_exec, flexql_free
 */
#include "../../include/flexql.h"
#include "../../include/network.hpp"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>

#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <string>
#include <sstream>
#include <vector>

/* Internal handle definition */
struct FlexQL {
    int sockfd = -1;
};

/* ── flexql_open ─────────────────────────────────────────── */
int flexql_open(const char *host, int port, FlexQL **db) {
    if (!host || !db) return FLEXQL_ERROR;

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return FLEXQL_ERROR;

    // Resolve host
    struct addrinfo hints{}, *res = nullptr;
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    char portStr[16];
    snprintf(portStr, sizeof(portStr), "%d", port);
    if (getaddrinfo(host, portStr, &hints, &res) != 0) {
        close(fd);
        return FLEXQL_ERROR;
    }

    if (connect(fd, res->ai_addr, res->ai_addrlen) < 0) {
        freeaddrinfo(res);
        close(fd);
        return FLEXQL_ERROR;
    }
    freeaddrinfo(res);

    FlexQL *handle = new FlexQL;
    handle->sockfd = fd;
    *db = handle;
    return FLEXQL_OK;
}

/* ── flexql_close ────────────────────────────────────────── */
int flexql_close(FlexQL *db) {
    if (!db) return FLEXQL_ERROR;
    if (db->sockfd >= 0) {
        // Politely tell server we're done
        sendFrame(db->sockfd, ".exit");
        close(db->sockfd);
        db->sockfd = -1;
    }
    delete db;
    return FLEXQL_OK;
}

/* ── Parse server response and invoke callback ───────────── */
/*
 * Response format:
 *   OK\n
 *   col1\tcol2\t...\n   (optional header)
 *   v1\tv2\t...\n       (zero or more data rows)
 *   END\n
 * OR:
 *   ERROR\n
 *   <message>\n
 *   END\n
 */
static int parseAndDispatch(
    const std::string &response,
    int (*callback)(void*, int, char**, char**),
    void *arg,
    char **errmsg)
{
    std::istringstream iss(response);
    std::string line;

    if (!std::getline(iss, line)) goto fail;
    // Trim \r
    if (!line.empty() && line.back() == '\r') line.pop_back();

    if (line == "ERROR") {
        std::string msg;
        std::getline(iss, msg);
        if (!msg.empty() && msg.back() == '\r') msg.pop_back();
        if (errmsg) {
            *errmsg = (char*)malloc(msg.size() + 1);
            strcpy(*errmsg, msg.c_str());
        }
        return FLEXQL_ERROR;
    }

    if (line != "OK") goto fail;

    {
        // Read header line
        std::string headerLine;
        if (!std::getline(iss, headerLine)) return FLEXQL_OK;
        if (!headerLine.empty() && headerLine.back() == '\r') headerLine.pop_back();
        if (headerLine == "END") return FLEXQL_OK;

        // Split header into column names
        std::vector<std::string> colNames;
        {
            std::istringstream hss(headerLine);
            std::string col;
            while (std::getline(hss, col, '\t'))
                colNames.push_back(col);
        }

        // Read data rows
        while (std::getline(iss, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line == "END") break;

            std::vector<std::string> vals;
            {
                std::istringstream vss(line);
                std::string v;
                while (std::getline(vss, v, '\t'))
                    vals.push_back(v);
            }

            if (!callback) continue;

            // Build C-style arrays
            int n = (int)colNames.size();
            std::vector<char*> cVals(n), cNames(n);
            for (int i = 0; i < n; ++i) {
                cVals[i]  = (char*)(i < (int)vals.size() ? vals[i].c_str() : "NULL");
                cNames[i] = (char*)colNames[i].c_str();
            }
            int ret = callback(arg, n, cVals.data(), cNames.data());
            if (ret != 0) break; // abort
        }
    }
    return FLEXQL_OK;

fail:
    if (errmsg) {
        const char *msg = "Malformed server response";
        *errmsg = (char*)malloc(strlen(msg) + 1);
        strcpy(*errmsg, msg);
    }
    return FLEXQL_ERROR;
}

/* ── flexql_exec ─────────────────────────────────────────── */
int flexql_exec(
    FlexQL *db,
    const char *sql,
    int (*callback)(void*, int, char**, char**),
    void *arg,
    char **errmsg)
{
    if (!db || db->sockfd < 0) {
        if (errmsg) {
            const char *m = "Invalid database handle";
            *errmsg = (char*)malloc(strlen(m) + 1);
            strcpy(*errmsg, m);
        }
        return FLEXQL_ERROR;
    }

    if (!sendFrame(db->sockfd, std::string(sql))) {
        if (errmsg) {
            const char *m = "Failed to send query";
            *errmsg = (char*)malloc(strlen(m) + 1);
            strcpy(*errmsg, m);
        }
        return FLEXQL_ERROR;
    }

    std::string response;
    if (!recvFrame(db->sockfd, response)) {
        if (errmsg) {
            const char *m = "Failed to receive response";
            *errmsg = (char*)malloc(strlen(m) + 1);
            strcpy(*errmsg, m);
        }
        return FLEXQL_ERROR;
    }

    return parseAndDispatch(response, callback, arg, errmsg);
}

/* ── flexql_free ─────────────────────────────────────────── */
void flexql_free(void *ptr) {
    free(ptr);
}
