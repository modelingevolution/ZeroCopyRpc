#pragma once
#include <atomic>
#include <stdexcept>

template <typename TId, unsigned int TSize>
class IDPool {
public:
    // Constructor to initialize the pool with IDs from 0 to 255
    IDPool() : _head(nullptr) {
        for (TId id = 0; id < TSize; ++id) {
            _nodes[id].id = id;
            _nodes[id].next = _head.load(std::memory_order_relaxed);
            _head.store(&_nodes[id], std::memory_order_relaxed);
        }
    }

    bool try_rent(const TId& target_id) {
        Node* old_head = _head.load(std::memory_order_acquire);
        Node* prev = nullptr;

        while (old_head) {
            // Look for the target ID in the free list
            if (old_head->id == target_id) {
                Node* next = old_head->next;

                // If it's at the head of the list
                if (prev == nullptr) {
                    if (_head.compare_exchange_weak(old_head, next,
                        std::memory_order_release, std::memory_order_relaxed)) {
                        return true;
                    }
                }
                // If it's in the middle/end of the list
                else {
                    prev->next = next;
                    return true;
                }
            }
            prev = old_head;
            old_head = old_head->next;
        }
        return false;
    }
    // Allocate an ID from the pool
    bool rent(TId& id) {
        Node* old_head = _head.load(std::memory_order_acquire);
        while (old_head) {
            Node* next = old_head->next;
            if (_head.compare_exchange_weak(old_head, next, std::memory_order_release, std::memory_order_relaxed)) {
                id = old_head->id;
                return true;
            }
        }
        return false; // No IDs available
    }

    // Release an ID back to the pool
    void returns(TId id) {
        if (id >= TSize) {
            throw std::out_of_range("ID out of range.");
        }
        Node* node = &_nodes[id];
        Node* old_head = _head.load(std::memory_order_acquire);
        do {
            node->next = old_head;
        } while (!_head.compare_exchange_weak(old_head, node, std::memory_order_release, std::memory_order_relaxed));
    }

    // Check if the pool is empty
    bool empty() const {
        return _head.load(std::memory_order_acquire) == nullptr;
    }

private:
    // Node structure representing each ID
    struct Node {
        TId id;
        Node* next;
    };

    // Atomic pointer to the head of the stack
    std::atomic<Node*> _head;

    // Fixed-size array of nodes (one for each ID)
    Node _nodes[TSize];
};