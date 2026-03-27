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
int flexql_exec(FlexQL     *db,
                const char *sql,
                int (*callback)(void *, int, char **, char **),
                void       *arg,
                char      **errmsg) {

    if (!db || db->sock < 0 || !sql) return FLEXQL_ERROR;

    std::string query = std::string(sql);
    if (query.back() != ';') query += ";";

    if (send(db->sock, query.c_str(), query.length(), 0) <= 0) return FLEXQL_ERROR;

    std::string pending;
    char buf[8192];
    bool done = false;
    bool hasError = false;
    std::string errorText;

    while (!done) {
        ssize_t n = read(db->sock, buf, sizeof(buf) - 1);
        if (n <= 0) break;
        buf[n] = '\0';
        pending.append(buf, n);

        size_t nlpos;
        while ((nlpos = pending.find('\n')) != std::string::npos) {
            std::string line = pending.substr(0, nlpos);
            pending.erase(0, nlpos + 1);

            if (!line.empty() && line.back() == '\r') line.pop_back();

            if (line == "END") {
                done = true;
                break;
            }

            if (line.rfind("ERROR|", 0) == 0) {
                hasError = true;
                errorText = line.substr(6);
                size_t first = errorText.find_first_not_of(' ');
                if (first != std::string::npos) errorText = errorText.substr(first);
                continue;
            }

            /* Split row data by spaces or '|' */
            if (callback && !line.empty()) {
                std::vector<std::string> columns;
                std::stringstream ss(line);
                std::string token;

                while (std::getline(ss, token, ' ')) {
                    if (!token.empty()) columns.push_back(token);
                }

                std::vector<char*> argv;
                for (auto &c : columns) argv.push_back((char*)c.c_str());

                if (callback(arg, (int)argv.size(), argv.data(), nullptr) != 0) {
                    done = true;
                    break;
                }
            }
        }
    }

    if (hasError && errmsg) {
        *errmsg = strdup(errorText.c_str());
    }

    return hasError ? FLEXQL_ERROR : (done ? FLEXQL_OK : FLEXQL_ERROR);
}

/* ── flexql_free ─────────────────────────────────────────── */
void flexql_free(void *ptr) {
    free(ptr);
}