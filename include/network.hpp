#pragma once
#include <string>
#include <cstdint>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cstring>

/* Simple framing: 4-byte big-endian length prefix + payload */

inline bool sendAll(int fd, const char *buf, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = ::send(fd, buf + sent, len - sent, MSG_NOSIGNAL);
        if (n <= 0) return false;
        sent += n;
    }
    return true;
}

inline bool recvAll(int fd, char *buf, size_t len) {
    size_t got = 0;
    while (got < len) {
        ssize_t n = ::recv(fd, buf + got, len - got, 0);
        if (n <= 0) return false;
        got += n;
    }
    return true;
}

inline bool sendFrame(int fd, const std::string &payload) {
    uint32_t len = htonl((uint32_t)payload.size());
    if (!sendAll(fd, (const char*)&len, 4)) return false;
    if (!payload.empty())
        if (!sendAll(fd, payload.data(), payload.size())) return false;
    return true;
}

inline bool recvFrame(int fd, std::string &payload) {
    uint32_t len = 0;
    if (!recvAll(fd, (char*)&len, 4)) return false;
    len = ntohl(len);
    if (len == 0) { payload.clear(); return true; }
    payload.resize(len);
    return recvAll(fd, &payload[0], len);
}
