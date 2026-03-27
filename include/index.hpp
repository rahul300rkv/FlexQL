#pragma once
#include <unordered_map>
#include <vector>
#include <string>

class PrimaryIndex {
    std::unordered_map<std::string, std::vector<size_t>> index;

public:
    void insert(const std::string& key, size_t pos) {
        index[key].push_back(pos);
    }

    bool hasKey(const std::string& key) {
        return index.find(key) != index.end();
    }

    std::vector<size_t> lookup(const std::string& key) {
        if (index.count(key)) return index[key];
        return {};
    }

    void clear() {
        index.clear();
    }
};