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

struct FlexQL {
    int sockfd = -1;
};

/* ── flexql_open ─────────────────────────────────────────── */
int flexql_open(const char *host, int port, FlexQL **db) {
    if (!host || !db) return FLEXQL_ERROR;

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return FLEXQL_ERROR;

    struct sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    if (inet_pton(AF_INET, host, &serv_addr.sin_addr) <= 0) {
        close(fd);
        return FLEXQL_ERROR;
    }

    if (connect(fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        close(fd);
        return FLEXQL_ERROR;
    }

    FlexQL *handle = new FlexQL;
    handle->sockfd = fd;
    *db = handle;
    return FLEXQL_OK;
}

/* ── flexql_close ────────────────────────────────────────── */
int flexql_close(FlexQL *db) {
    if (!db) return FLEXQL_ERROR;
    if (db->sockfd >= 0) {
        close(db->sockfd);
        db->sockfd = -1;
    }
    delete db;
    return FLEXQL_OK;
}

/* ── Updated parseAndDispatch for Benchmark Protocol ─────── */
static int parseAndDispatch(
    int sockfd,
    flexql_callback callback,
    void *arg,
    char **errmsg)
{
    // Using fdopen for easier line-by-line processing
    FILE* stream = fdopen(dup(sockfd), "r");
    if (!stream) return FLEXQL_ERROR;

    char buffer[8192];
    int result = FLEXQL_OK;

    while (fgets(buffer, sizeof(buffer), stream)) {
        std::string line(buffer);
        
        // Clean trailing whitespace/newlines
        line.erase(line.find_last_not_of(" \n\r\t") + 1);

        if (line.empty()) continue;
        if (line == "END") break;
        if (line == "OK") continue; // Benchmark doesn't want "OK" in callback

        // Handle Errors
        if (line.find("ERR ") == 0 || line.find("ERROR") == 0) {
            std::string msg = (line.length() > 4) ? line.substr(4) : line;
            if (errmsg) {
                *errmsg = strdup(msg.c_str());
            }
            result = FLEXQL_ERROR;
            break;
        }

        // Handle Rows: Strip "ROW " and pass as single string (argc=1)
        if (line.find("ROW ") == 0) {
            std::string content = line.substr(4);
            
            char* val_ptr = (char*)content.c_str();
            char* values[1] = { val_ptr };
            char* names[1] = { (char*)"row_content" };

            if (callback) {
                // Critical: argc is 1, argv[0] is the whole space-joined string
                if (callback(arg, 1, values, names) != 0) break;
            }
        }
    }

    fclose(stream);
    return result;
}

/* ── flexql_exec ─────────────────────────────────────────── */
int flexql_exec(
    FlexQL *db,
    const char *sql,
    flexql_callback callback,
    void *arg,
    char **errmsg)
{
    if (!db || db->sockfd < 0) return FLEXQL_ERROR;

    std::string query = std::string(sql) + "\n";
    if (send(db->sockfd, query.c_str(), query.length(), 0) < 0) {
        return FLEXQL_ERROR;
    }

    return parseAndDispatch(db->sockfd, callback, arg, errmsg);
}

void flexql_free(void *ptr) {
    if (ptr) free(ptr);
}