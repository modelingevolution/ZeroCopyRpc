#pragma once
#include <atomic>
#include <stdexcept>

template <typename TId, unsigned int TSize>
class IDPool {
public:
    IDPool() : _head(nullptr) {
        // Initialize in reverse order to maintain same complexity
        unsigned int id = TSize;
        do {
            id = id - 1;
            _nodes[id].id = static_cast<TId>(id);
            _nodes[id].next = _head;
            _head = &_nodes[id];
        } while (id != 0);
        // Memory fence to ensure visibility of initialization
        std::atomic_thread_fence(std::memory_order_seq_cst);
    }

    bool try_rent(const TId& target_id) {
        if (target_id >= TSize) {
            return false;
        }

        Node* current = _head.load(std::memory_order_acquire);
        Node* prev = nullptr;

        while (current) {
            if (current->id == target_id) {
                if (prev == nullptr) {
                    // If target is at head, try to update head
                    Node* next = current->next;
                    if (_head.compare_exchange_strong(current, next,
                        std::memory_order_release,
                        std::memory_order_relaxed)) {
                        return true;
                    }
                    // If CAS failed, start over
                    current = _head.load(std::memory_order_acquire);
                    prev = nullptr;
                    continue;
                }

                // For nodes in the middle/end, use atomic marker
                Node* next = current->next;
                current->in_use.store(true, std::memory_order_release);

                // Verify the list hasn't been modified
                if (prev->next != current || current->next != next) {
                    current->in_use.store(false, std::memory_order_release);
                    current = _head.load(std::memory_order_acquire);
                    prev = nullptr;
                    continue;
                }

                prev->next = next;
                return true;
            }
            prev = current;
            current = current->next;
        }
        return false;
    }

    bool rent(TId& id) {
        Node* old_head;
        while ((old_head = _head.load(std::memory_order_acquire)) != nullptr) {
            Node* next = old_head->next;
            if (_head.compare_exchange_strong(old_head, next,
                std::memory_order_release,
                std::memory_order_relaxed)) {
                id = old_head->id;
                old_head->in_use.store(true, std::memory_order_release);
                return true;
            }
        }
        return false;
    }

    void returns(TId id) {
        if (id >= TSize) {
            throw std::out_of_range("ID out of range");
        }

        Node* node = &_nodes[id];

        // Verify the ID wasn't already returned
        bool expected = true;
        if (!node->in_use.compare_exchange_strong(expected, false,
            std::memory_order_release,
            std::memory_order_relaxed)) {
            throw std::runtime_error("ID already returned to pool");
        }

        Node* old_head;
        do {
            old_head = _head.load(std::memory_order_acquire);
            node->next = old_head;
        } while (!_head.compare_exchange_weak(old_head, node,
            std::memory_order_release,
            std::memory_order_relaxed));
    }

    bool empty() const {
        return _head.load(std::memory_order_acquire) == nullptr;
    }

private:
    struct Node {
        TId id;
        Node* next;
        std::atomic<bool> in_use{ false };  // Track if node is currently rented
    };

    std::atomic<Node*> _head;
    Node _nodes[TSize];
};