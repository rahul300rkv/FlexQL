/*
 * FlexQL interactive REPL client.
 * Usage:  ./client [host [port]]
 */
#include "../../include/flexql.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <iostream>

static int printRow(void *, int argc, char **argv, char **) {
    for (int i = 0; i < argc; ++i) {
        if (i) printf(" | ");
        printf("%s", argv[i] ? argv[i] : "NULL");
    }
    printf("\n");
    return 0;
}

int main(int argc, char *argv[]) {
    const char *host = "127.0.0.1";
    int         port = 9000;
    if (argc >= 2) host = argv[1];
    if (argc >= 3) port = std::atoi(argv[2]);

    FlexQL *db = nullptr;
    if (flexql_open(host, port, &db) != FLEXQL_OK) {
        fprintf(stderr, "Cannot connect to %s:%d\n", host, port);
        return 1;
    }
    printf("Connected to FlexQL at %s:%d\n", host, port);
    printf("Type SQL statements ending with ';'  (Ctrl-D to quit)\n\n");

    std::string buf;
    while (std::getline(std::cin, buf)) {
        if (buf.empty()) continue;
        /* Ensure statement ends with ';' */
        if (buf.back() != ';') buf += ';';

        char *err = nullptr;
        int rc = flexql_exec(db, buf.c_str(), printRow, nullptr, &err);
        if (rc != FLEXQL_OK) {
            fprintf(stderr, "Error: %s\n", err ? err : "unknown");
            flexql_free(err);
        }
    }

    flexql_close(db);
    return 0;
}