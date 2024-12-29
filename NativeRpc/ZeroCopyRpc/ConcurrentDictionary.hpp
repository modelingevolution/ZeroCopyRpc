#pragma once

#include <optional>
#include <shared_mutex>
#include <unordered_map>
#include <utility>  // for std::pair
#include <memory>   // for std::shared_ptr if needed



template <typename Key, typename Value>
class ConcurrentDictionary {
private:
    std::unordered_map<Key, Value> map;
    mutable std::shared_mutex mtx;
public:
    // Inserts or updates a key-value pair
    void InsertOrUpdate(const Key& key, const Value& value) {
        std::unique_lock<std::shared_mutex> lock(mtx);
        map[key] = value;
    }
    // Tries to get the value associated with a key
    bool TryGetValue(const Key& key, Value& value) const {
        std::shared_lock<std::shared_mutex> lock(mtx);
        auto it = map.find(key);
        if (it != map.end()) {
            value = it->second; // Copy the value to the provided reference
            return true;        // Indicate that the key was found
        }
        return false;           // Indicate that the key was not found
    }
    // Removes a key-value pair
    bool Remove(const Key& key) {
        std::unique_lock<std::shared_mutex> lock(mtx);
        return map.erase(key) > 0;
    }
    // Checks if a key exists
    bool ContainsKey(const Key& key) const {
        std::shared_lock<std::shared_mutex> lock(mtx);
        return map.find(key) != map.end();
    }
    // Clears the dictionary
    void Clear() {
        std::unique_lock<std::shared_mutex> lock(mtx);
        map.clear();
    }
};
