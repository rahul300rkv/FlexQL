#pragma once
#include <unordered_map>
#include <list>
#include <string>
#include <optional>

/*
 * LRU cache backed by a doubly-linked list + hashmap.
 *
 *   put()  — insert / update, evict LRU entry when over capacity
 *   get()  — return value on hit and promote to MRU; nullopt on miss
 *   invalidateTable(name) — evict every key that starts with "name:"
 *                           called on INSERT / DELETE to keep cache coherent
 */
template<typename T>
class Cache {
public:
    explicit Cache(size_t cap = 256) : cap_(cap) {}

    void put(const std::string &key, const T &value) {
        auto it = map_.find(key);
        if (it != map_.end()) {
            it->second->value = value;
            list_.splice(list_.begin(), list_, it->second);
            return;
        }
        if (list_.size() >= cap_) {
            map_.erase(list_.back().key);
            list_.pop_back();
        }
        list_.push_front({key, value});
        map_[key] = list_.begin();
    }

    std::optional<T> get(const std::string &key) {
        auto it = map_.find(key);
        if (it == map_.end()) return std::nullopt;
        list_.splice(list_.begin(), list_, it->second);
        return it->second->value;
    }

    /* Evict all entries whose key begins with "tableName:" */
    void invalidateTable(const std::string &tableName) {
        std::string prefix = tableName + ":";
        for (auto it = list_.begin(); it != list_.end(); ) {
            if (it->key.compare(0, prefix.size(), prefix) == 0) {
                map_.erase(it->key);
                it = list_.erase(it);
            } else {
                ++it;
            }
        }
    }

    void clear() {
        list_.clear();
        map_.clear();
    }

private:
    struct Entry { std::string key; T value; };

    size_t cap_;
    std::list<Entry>                                           list_;
    std::unordered_map<std::string, typename std::list<Entry>::iterator> map_;
};
