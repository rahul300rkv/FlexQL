#include "flexql.h"
#include <iostream>
#include <string>

int callback(void*, int cols, char** vals, char** names) {
    for (int i = 0; i < cols; i++) {
        std::cout << names[i] << ": " << vals[i] << " ";
    }
    std::cout << std::endl;
    return 0;
}

int main() {
    flexql_db* db;
    flexql_open("localhost", 9000, &db);

    while (true) {
        std::cout << "flexql> ";
        std::string query;
        std::getline(std::cin, query);

        if (query == "exit") break;

        char* err = nullptr;

        int rc = flexql_exec(db, query.c_str(), callback, NULL, &err);

        if (rc != FLEXQL_OK) {
            std::cout << "Error: " << (err ? err : "Unknown") << std::endl;
            if (err) flexql_free(err);
        }
    }

    flexql_close(db);
}