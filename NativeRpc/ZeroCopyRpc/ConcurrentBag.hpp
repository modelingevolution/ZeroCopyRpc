#pragma once

#include <atomic>
#include <array>
#include <optional>

template <typename T, size_t Capacity>
class ConcurrentBag {
    static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable");

    // Internal node structure to track state
    struct Node {
        std::atomic<T> value;
        std::atomic<size_t> next;

        Node() : value(T{}), next(static_cast<size_t>(-1)) {}
    };

public:
    ConcurrentBag() {
        // Initialize the free list
        for (size_t i = 0; i < Capacity - 1; ++i) {
            nodes[i].next.store(i + 1, std::memory_order_relaxed);
        }
        nodes[Capacity - 1].next.store(static_cast<size_t>(-1), std::memory_order_relaxed);

        // Initialize head pointers
        free_list_head.store(0, std::memory_order_relaxed);
        items_head.store(static_cast<size_t>(-1), std::memory_order_relaxed);
    }

    bool push(const T& item) {
        size_t new_node_idx = pop_from_free_list();
        if (new_node_idx == static_cast<size_t>(-1)) {
            return false;  // Bag is full
        }

        // Store the item
        nodes[new_node_idx].value.store(item, std::memory_order_relaxed);

        // Add to the items list
        size_t old_head = items_head.load(std::memory_order_relaxed);
        do {
            nodes[new_node_idx].next.store(old_head, std::memory_order_relaxed);
        } while (!items_head.compare_exchange_weak(old_head, new_node_idx,
            std::memory_order_release,
            std::memory_order_relaxed));
        return true;
    }

    bool try_pop(T& result) {
        size_t head = items_head.load(std::memory_order_acquire);
        size_t next;

        while (head != static_cast<size_t>(-1)) {
            next = nodes[head].next.load(std::memory_order_relaxed);

            if (items_head.compare_exchange_weak(head, next,
                std::memory_order_release,
                std::memory_order_relaxed)) {
                // Successfully removed node from list
                result = nodes[head].value.load(std::memory_order_relaxed);
                push_to_free_list(head);
                return true;
            }
        }
        return false;  // Bag is empty
    }
    bool empty() const {
        return items_head.load(std::memory_order_acquire) == static_cast<size_t>(-1);
    }

    bool full() const {
        return free_list_head.load(std::memory_order_acquire) == static_cast<size_t>(-1);
    }
    bool remove(const T& item) {
        size_t current = items_head.load(std::memory_order_acquire);
        size_t previous = static_cast<size_t>(-1);

        while (current != static_cast<size_t>(-1)) {
            T current_value = nodes[current].value.load(std::memory_order_relaxed);
            size_t next = nodes[current].next.load(std::memory_order_relaxed);

            if (current_value == item) {
                // Try to remove the node
                if (previous == static_cast<size_t>(-1)) {
                    // Removing from head
                    if (items_head.compare_exchange_strong(current, next,
                        std::memory_order_release,
                        std::memory_order_relaxed)) {
                        push_to_free_list(current);
                        return true;
                    }
                }
                else {
                    // Removing from middle/end
                    if (nodes[previous].next.compare_exchange_strong(current, next,
                        std::memory_order_release,
                        std::memory_order_relaxed)) {
                        push_to_free_list(current);
                        return true;
                    }
                }
            }
            previous = current;
            current = next;
        }
        return false;
    }

    class Iterator {
    public:
        explicit Iterator(const ConcurrentBag& bag, size_t start = static_cast<size_t>(-1))
            : bag_(bag), current_(start == static_cast<size_t>(-1) ?
                bag.items_head.load(std::memory_order_acquire) : start) {
        }

        bool is_valid() const { return current_ != static_cast<size_t>(-1); }

        T current_item() const {
            if (!is_valid()) {
                throw std::out_of_range("Iterator out of range");
            }
            return bag_.nodes[current_].value.load(std::memory_order_acquire);
        }

        Iterator& operator++(int) {
            if (is_valid()) {
                current_ = bag_.nodes[current_].next.load(std::memory_order_acquire);
            }
            return *this;
        }

    private:
        const ConcurrentBag& bag_;
        size_t current_;
    };

    Iterator begin() const { return Iterator(*this); }

private:
    size_t pop_from_free_list() {
        size_t old_head = free_list_head.load(std::memory_order_acquire);
        size_t next;

        while (old_head != static_cast<size_t>(-1)) {
            next = nodes[old_head].next.load(std::memory_order_relaxed);
            if (free_list_head.compare_exchange_weak(old_head, next,
                std::memory_order_release,
                std::memory_order_relaxed)) {
                return old_head;
            }
        }
        return static_cast<size_t>(-1);  // Free list is empty
    }

    void push_to_free_list(size_t index) {
        size_t old_head = free_list_head.load(std::memory_order_relaxed);
        do {
            nodes[index].next.store(old_head, std::memory_order_relaxed);
        } while (!free_list_head.compare_exchange_weak(old_head, index,
            std::memory_order_release,
            std::memory_order_relaxed));
    }

    // Storage
    std::array<Node, Capacity> nodes;
    std::atomic<size_t> free_list_head;
    std::atomic<size_t> items_head;
};