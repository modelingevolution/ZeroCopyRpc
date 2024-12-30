#include <gtest/gtest.h>
#include <thread>
#include <vector>

#include "ConcurrentBag.hpp"

// Test struct to use as T
struct TestItem {
    int value;
    bool operator==(const TestItem& other) const {
        return value == other.value;
    }
};

class ConcurrentBagTest : public ::testing::Test {
protected:
    static const size_t TEST_CAPACITY = 256;
    ConcurrentBag<TestItem, TEST_CAPACITY> bag;
};

// Test basic push operation
TEST_F(ConcurrentBagTest, PushSingleItem) {
    TestItem item{ 42 };
    EXPECT_TRUE(bag.empty());
    EXPECT_TRUE(bag.push(item));
    EXPECT_FALSE(bag.empty());
}

// Test basic pop operation
TEST_F(ConcurrentBagTest, PushAndPopSingleItem) {
    TestItem item{ 42 };
    EXPECT_TRUE(bag.push(item));

    TestItem popped;
    EXPECT_TRUE(bag.try_pop(popped));
    EXPECT_EQ(popped.value, item.value);
    EXPECT_TRUE(bag.empty());
}

// Test capacity limits
TEST_F(ConcurrentBagTest, CapacityLimits) {
    TestItem item{ 1 };

    // Fill the bag to capacity
    for (size_t i = 0; i < TEST_CAPACITY; ++i) {
        EXPECT_TRUE(bag.push(item));
    }

    // Verify bag is full
    EXPECT_TRUE(bag.full());
    EXPECT_FALSE(bag.push(item));  // Should fail as bag is full
}

// Test removal operation
TEST_F(ConcurrentBagTest, RemoveItem) {
    TestItem items[] = { {1}, {2}, {3} };

    for (const auto& item : items) {
        EXPECT_TRUE(bag.push(item));
    }

    // Remove middle item
    EXPECT_TRUE(bag.remove({ 2 }));

    // Verify remaining items
    std::vector<TestItem> remaining;
    TestItem popped;
    while (bag.try_pop(popped)) {
        remaining.push_back(popped);
    }

    EXPECT_EQ(remaining.size(), 2);
    EXPECT_TRUE(std::find_if(remaining.begin(), remaining.end(),
        [](const TestItem& item) { return item.value == 2; }) == remaining.end());
}

// Test iterator
TEST_F(ConcurrentBagTest, Iterator) {
    // Push some items
    for (int i = 0; i < 5; ++i) {
        EXPECT_TRUE(bag.push(TestItem{ i }));
    }

    // Use iterator to verify items
    auto it = bag.begin();
    std::vector<int> found_values;

    while (it.is_valid()) {
        found_values.push_back(it.current_item().value);
        it++;
    }

    EXPECT_EQ(found_values.size(), 5);
    for (int i = 0; i < 5; ++i) {
        EXPECT_TRUE(std::find(found_values.begin(), found_values.end(), i) != found_values.end());
    }
}

// Test concurrent operations
TEST_F(ConcurrentBagTest, ConcurrentPushPop) {
    const int NUM_THREADS = 4;
    const int ITEMS_PER_THREAD = 1000;

    std::vector<std::thread> producers;
    std::vector<std::thread> consumers;
    std::atomic<int> total_consumed{ 0 };

    // Create producer threads
    for (int i = 0; i < NUM_THREADS; ++i) {
        producers.emplace_back([this, i, ITEMS_PER_THREAD]() {
            for (int j = 0; j < ITEMS_PER_THREAD; ++j) {
                TestItem item{ i * ITEMS_PER_THREAD + j };
                while (!bag.push(item)) {
                    std::this_thread::yield();
                }
            }
            });
    }

    // Create consumer threads
    for (int i = 0; i < NUM_THREADS; ++i) {
        consumers.emplace_back([this, &total_consumed]() {
            TestItem item;
            while (total_consumed.load() < NUM_THREADS * ITEMS_PER_THREAD) {
                if (bag.try_pop(item)) {
                    total_consumed.fetch_add(1);
                }
                else {
                    std::this_thread::yield();
                }
            }
            });
    }

    // Join all threads
    for (auto& t : producers) t.join();
    for (auto& t : consumers) t.join();

    EXPECT_EQ(total_consumed.load(), NUM_THREADS * ITEMS_PER_THREAD);
    EXPECT_TRUE(bag.empty());
}

// Test empty bag behavior
TEST_F(ConcurrentBagTest, EmptyBagOperations) {
    TestItem item;
    EXPECT_FALSE(bag.try_pop(item));
    EXPECT_FALSE(bag.remove({ 42 }));
    EXPECT_TRUE(bag.empty());
    EXPECT_FALSE(bag.full());

    auto it = bag.begin();
    EXPECT_FALSE(it.is_valid());
}

// Test push-pop cycles
TEST_F(ConcurrentBagTest, PushPopCycles) {
    const int CYCLES = 1000;

    for (int i = 0; i < CYCLES; ++i) {
        TestItem item{ i };
        EXPECT_TRUE(bag.push(item));

        TestItem popped;
        EXPECT_TRUE(bag.try_pop(popped));
        EXPECT_EQ(popped.value, i);
    }

    EXPECT_TRUE(bag.empty());
}