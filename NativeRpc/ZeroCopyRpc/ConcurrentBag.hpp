#pragma once

#include <optional>
#include <shared_mutex>
#include <unordered_map>
#include <utility>  // for std::pair
#include <memory>   // for std::shared_ptr if needed
#include <atomic>
#include <array>

template <typename T, size_t Capacity>
class ConcurrentBag {
    static T empty_value() { return T{}; }
public:
    class Iterator {
    private:
        const ConcurrentBag<T, Capacity>& bag; // Reference to the bag being iterated
        size_t current;                        // Current index in the bag

    public:
        explicit Iterator(const ConcurrentBag<T, Capacity>& bag, size_t start = 0)
            : bag(bag), current(start) {
            findNextValid(); // Ensure starting at a valid item
        }

        // Check if the iterator is valid (i.e., not out of bounds)
        bool is_valid() const {
            return current < Capacity;
        }

        // Access the current item
        T current_item() const {
            if (!is_valid()) {
                throw std::out_of_range("Iterator out of range");
            }
            return bag.items[current].load(std::memory_order_acquire);
        }

        // Move to the next valid item
        Iterator operator++(int) {
            if (current >= Capacity) {
                throw std::out_of_range("Iterator out of range");
            }
            ++current;
            findNextValid();
            return *this;
        }
        Iterator operator--(int) {
            if (current == 0) {
                throw std::out_of_range("Iterator out of range");
            }
            --current;
            findPrvValid();
            return *this;
        }



    private:
        // Helper to advance to the next non-empty slot
        inline void findNextValid() {
            while (current < Capacity && bag.items[current].load(std::memory_order_acquire) == empty_value()) {
                ++current;
            }
        }
        inline void findPrvValid() {
            while (current > 0 && bag.items[current].load(std::memory_order_acquire) == empty_value()) {
                --current;
            }
        }
    };
    ConcurrentBag() {

        // Initialize free list
        for (size_t i = 0; i < Capacity; ++i) {
            free_list[i] = i; // Free list holds offsets
        }
        free_head.store(Capacity - 1, std::memory_order_relaxed); // Head points to the last free slot
    }

    Iterator begin()
    {
        return Iterator(*this);
    }
    // Push an item into the bag
    bool push(const T& item) {
        size_t index;
        if (!pop_from_free_list(index)) {
            return false; // Bag is full
        }
        items[index].store(item, std::memory_order_release);
        return true;
    }
    // Try to pop an item from the bag. Returns true if successful, false otherwise.
    bool try_pop(T& value) {
        size_t index;
        if (!push_to_free_list(index)) {
            return false; // Bag is empty
        }
        value = items[index].load(std::memory_order_acquire);
        return true;
    }
    // Pop an item from the bag
    //std::optional<T> pop() {
    //    size_t index;
    //    if (!push_to_free_list(index)) {
    //        return std::nullopt; // Bag is empty
    //    }
    //    return items[index].load(std::memory_order_acquire);
    //}

    // Removes the specified item from the bag.
    // Returns true if the item was found and removed, false otherwise.
    bool remove(const T& item) {
        for (size_t i = 0; i < Capacity; ++i) {
            // Check if the current slot matches the item
            T current = items[i].load(std::memory_order_acquire);
            if (current == item) {
                T empty = empty_value(); // Default-constructed value represents "empty"
                // Attempt to replace the item with the empty value
                if (items[i].compare_exchange_strong(current, empty, std::memory_order_release, std::memory_order_relaxed)) {
                    // Successfully removed the item, push the index back into the free list
                    return push_to_free_list(i);
                }
            }
        }
        return false; // Item not found
    }
    // Check if the bag is full
    bool full() const {
        return free_head.load(std::memory_order_acquire) == static_cast<size_t>(-1);
    }

    // Check if the bag is empty
    bool empty() const {
        return free_head.load(std::memory_order_acquire) == Capacity - 1;
    }

private:
    // Attempt to pop from the free list
    bool pop_from_free_list(size_t& index) {
        size_t old_head = free_head.load(std::memory_order_acquire);
        while (old_head != static_cast<size_t>(-1)) {
            size_t new_head = old_head - 1;
            if (free_head.compare_exchange_weak(old_head, new_head, std::memory_order_release, std::memory_order_relaxed)) {
                index = free_list[old_head];
                return true;
            }
        }
        return false; // Free list is empty
    }

    // Attempt to push back to the free list
    bool push_to_free_list(size_t index) {
        size_t old_head = free_head.load(std::memory_order_acquire);
        while (old_head < Capacity - 1) {
            size_t new_head = old_head + 1;
            free_list[new_head] = index;
            if (free_head.compare_exchange_weak(old_head, new_head, std::memory_order_release, std::memory_order_relaxed)) {
                return true;
            }
        }
        return false; // Free list is full
    }

    // Fixed-size storage for items
    std::array<std::atomic<T>, Capacity> items;

    // Fixed-size free list for managing offsets
    std::array<size_t, Capacity> free_list;

    // Atomic index for the free list head
    std::atomic<size_t> free_head;
};