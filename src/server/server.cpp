#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <string>
#include <vector>
#include <sstream>
#include <cstring>

/*
    You must implement this function elsewhere.
    It should:
    - Parse SQL
    - Execute using StorageEngine
    - Fill ResultSet
*/
struct ResultSet {
    std::vector<std::string> columns;
    std::vector<std::vector<std::string>> rows;
};

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
        for (size_t i = 0; i < row.size(); i++) {
            out << row[i];
            if (i != row.size() - 1) out << "|";
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