#pragma once
#include <unordered_map>
#include <string>

template<typename T>
class Cache {
public:
    std::unordered_map<std::string, T> store;

    void put(const std::string& key, const T& value) {
        store[key] = value;
    }

    bool exists(const std::string& key) {
        return store.find(key) != store.end();
    }

    T get(const std::string& key) {
        return store[key];
    }

    void invalidateTable(const std::string&) {
        // simple full clear (acceptable)
        store.clear();
    }
};