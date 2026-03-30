#include "../../include/flexql.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <string>
#include <sstream>
#include <vector>

/*
 * Internal handle.
 * recvBuf persists between flexql_exec calls so that bytes belonging
 * to the next response are never silently discarded when a recv() call
 * returns more data than the current response needs.
 */
struct FlexQL {
    int         sockfd  = -1;
    std::string recvBuf;
};

/* ── flexql_open ─────────────────────────────────────────── */
int flexql_open(const char *host, int port, FlexQL **db) {
    if (!host || !db) return FLEXQL_ERROR;

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return FLEXQL_ERROR;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons((uint16_t)port);
    if (inet_pton(AF_INET, host, &addr.sin_addr) <= 0) {
        close(fd); return FLEXQL_ERROR;
    }
    if (connect(fd, (sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd); return FLEXQL_ERROR;
    }

    FlexQL *handle = new FlexQL;
    handle->sockfd = fd;
    *db = handle;
    return FLEXQL_OK;
}

/* ── flexql_close ────────────────────────────────────────── */
int flexql_close(FlexQL *db) {
    if (!db) return FLEXQL_ERROR;
    if (db->sockfd >= 0) { close(db->sockfd); db->sockfd = -1; }
    delete db;
    return FLEXQL_OK;
}

/* ── Split a tab-delimited line into fields ──────────────── */
static std::vector<std::string> splitTabs(const std::string &line) {
    std::vector<std::string> fields;
    std::string cur;
    for (char c : line) {
        if (c == '\t') { fields.push_back(cur); cur.clear(); }
        else           cur += c;
    }
    fields.push_back(cur);
    return fields;
}

/*
 * Read exactly one complete response into `response`.
 *
 * A response ends when we see a line whose content is exactly "END"
 * or starts with "ERR\t".  All bytes after that line stay in
 * db->recvBuf for the next call — this is the key fix for the TCP
 * pipeline bug where multiple responses arrive in one recv() chunk.
 *
 * Returns false only when the socket closes before a terminator arrives.
 */
static bool readResponse(FlexQL *db, std::string &response) {
    response.clear();
    char tmp[4096];

    while (true) {
        /* Scan the persistent buffer line-by-line */
        size_t scan = 0;
        while (scan < db->recvBuf.size()) {
            size_t nl = db->recvBuf.find('\n', scan);
            if (nl == std::string::npos) break;

            std::string line = db->recvBuf.substr(scan, nl - scan);
            if (!line.empty() && line.back() == '\r') line.pop_back();

            response += line + "\n";
            scan = nl + 1;

            if (line == "END") {
                db->recvBuf.erase(0, scan);
                return true;
            }
            if (line.size() >= 4 && line.compare(0, 4, "ERR\t") == 0) {
                db->recvBuf.erase(0, scan);
                return true;
            }
        }
        /* No terminator yet — read more bytes */
        ssize_t n = recv(db->sockfd, tmp, sizeof(tmp) - 1, 0);
        if (n <= 0) return false;
        tmp[n] = '\0';
        db->recvBuf.append(tmp, (size_t)n);
    }
}

/* ── flexql_exec ─────────────────────────────────────────── */
int flexql_exec(FlexQL *db,
                const char *sql,
                int (*callback)(void *, int, char **, char **),
                void *arg,
                char **errmsg) {

    if (!db || db->sockfd < 0 || !sql) {
        if (errmsg) *errmsg = strdup("Invalid database handle");
        return FLEXQL_ERROR;
    }

    std::string query = sql;
    while (!query.empty() &&
           (query.back() == ';' || query.back() == ' ' || query.back() == '\n'))
        query.pop_back();
    query += "\n";

    if (send(db->sockfd, query.c_str(), query.size(), MSG_NOSIGNAL) <= 0) {
        if (errmsg) *errmsg = strdup("Failed to send query");
        return FLEXQL_ERROR;
    }

    std::string response;
    if (!readResponse(db, response)) {
        if (errmsg) *errmsg = strdup("Connection closed by server");
        return FLEXQL_ERROR;
    }

    std::vector<std::string> colNames;
    int result = FLEXQL_OK;

    std::istringstream ss(response);
    std::string line;
    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;

        auto fields = splitTabs(line);
        if (fields.empty()) continue;

        const std::string &tag = fields[0];

        if (tag == "HEADER") {
            colNames.clear();
            for (size_t i = 1; i < fields.size(); ++i)
                if (!fields[i].empty()) colNames.push_back(fields[i]);

        } else if (tag == "ROW") {
            if (!callback) continue;

            std::vector<std::string> vals;
            for (size_t i = 1; i < fields.size(); ++i)
                vals.push_back(fields[i]);

            std::vector<char *> argv_ptrs, col_ptrs;
            for (auto &v : vals)     argv_ptrs.push_back(const_cast<char *>(v.c_str()));
            for (auto &c : colNames) col_ptrs.push_back(const_cast<char *>(c.c_str()));

            int argc = (int)argv_ptrs.size();
            while ((int)col_ptrs.size() < argc)
                col_ptrs.push_back(const_cast<char *>(""));

            if (callback(arg, argc, argv_ptrs.data(), col_ptrs.data()) != 0)
                break;

        } else if (tag == "ERR") {
            std::string msg = (fields.size() >= 2) ? fields[1] : "Unknown error";
            if (errmsg) *errmsg = strdup(msg.c_str());
            result = FLEXQL_ERROR;
            break;

        } else if (tag == "END") {
            break;
        }
    }

    return result;
}

/* ── flexql_free ─────────────────────────────────────────── */
void flexql_free(void *ptr) {
    free(ptr);
}
