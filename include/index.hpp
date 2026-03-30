#pragma once
#include <unordered_map>
#include <string>
#include <vector>

/*
 * PrimaryIndex maps primary-key string → row indices.
 * NOTE: After any deletion that shifts row positions, the index
 * must be rebuilt. The storage engine handles this in purgeExpiredRows().
 */
class PrimaryIndex {
    std::unordered_map<std::string, std::vector<size_t>> idx_;

public:
    void insert(const std::string &key, size_t pos) {
        idx_[key].push_back(pos);
    }

    bool hasKey(const std::string &key) const {
        return idx_.find(key) != idx_.end();
    }

    std::vector<size_t> lookup(const std::string &key) const {
        auto it = idx_.find(key);
        if (it != idx_.end()) return it->second;
        return {};
    }

    void clear() { idx_.clear(); }

    void erase(const std::string &key) { idx_.erase(key); }
};
