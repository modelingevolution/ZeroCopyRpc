#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include "ConcurrentDictionary.hpp"

class ConcurrentDictionaryTest : public ::testing::Test {
protected:
    ConcurrentDictionary<int, std::string> dict;
};

// Basic Operations Tests
TEST_F(ConcurrentDictionaryTest, InsertAndRetrieve) {
    dict.InsertOrUpdate(1, "one");
    auto value = dict.TryGetValue(1);
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(*value, "one");
}

TEST_F(ConcurrentDictionaryTest, NonExistentKey) {
    auto value = dict.TryGetValue(999);
    ASSERT_FALSE(value.has_value());
}

TEST_F(ConcurrentDictionaryTest, UpdateExistingKey) {
    dict.InsertOrUpdate(1, "one");
    dict.InsertOrUpdate(1, "new_one");
    auto value = dict.TryGetValue(1);
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(*value, "new_one");
}

TEST_F(ConcurrentDictionaryTest, RemoveExistingKey) {
    dict.InsertOrUpdate(1, "one");
    EXPECT_TRUE(dict.Remove(1));
    EXPECT_FALSE(dict.ContainsKey(1));
}

TEST_F(ConcurrentDictionaryTest, RemoveNonExistentKey) {
    EXPECT_FALSE(dict.Remove(999));
}

TEST_F(ConcurrentDictionaryTest, Clear) {
    dict.InsertOrUpdate(1, "one");
    dict.InsertOrUpdate(2, "two");
    dict.Clear();
    EXPECT_FALSE(dict.ContainsKey(1));
    EXPECT_FALSE(dict.ContainsKey(2));
}

TEST_F(ConcurrentDictionaryTest, ContainsKey) {
    dict.InsertOrUpdate(1, "one");
    EXPECT_TRUE(dict.ContainsKey(1));
    EXPECT_FALSE(dict.ContainsKey(2));
}

// Thread Safety Tests
TEST_F(ConcurrentDictionaryTest, ConcurrentReads) {
    dict.InsertOrUpdate(1, "one");

    std::vector<std::thread> threads;
    const int numThreads = 10;
    std::atomic<int> successfulReads{ 0 };

    for (int i = 0; i < numThreads; ++i) {
        threads.emplace_back([&]() {
            auto value = dict.TryGetValue(1);
            if (value.has_value() && *value == "one") {
                successfulReads++;
            }
            });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    EXPECT_EQ(successfulReads, numThreads);
}

TEST_F(ConcurrentDictionaryTest, ConcurrentWrites) {
    std::vector<std::thread> threads;
    const int numThreads = 10;
    const int numOperationsPerThread = 100;

    for (int i = 0; i < numThreads; ++i) {
        threads.emplace_back([&, i]() {
            for (int j = 0; j < numOperationsPerThread; ++j) {
                dict.InsertOrUpdate(i * numOperationsPerThread + j,
                    std::to_string(i * numOperationsPerThread + j));
            }
            });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    // Verify all insertions
    int count = 0;
    for (int i = 0; i < numThreads; ++i) {
        for (int j = 0; j < numOperationsPerThread; ++j) {
            int key = i * numOperationsPerThread + j;
            if (dict.ContainsKey(key)) {
                count++;
            }
        }
    }

    EXPECT_EQ(count, numThreads * numOperationsPerThread);
}

TEST_F(ConcurrentDictionaryTest, ConcurrentReadWriteOperations) {
    std::vector<std::thread> threads;
    const int numThreads = 10;
    std::atomic<int> successfulOperations{ 0 };

    // Insert initial value
    dict.InsertOrUpdate(1, "initial");

    for (int i = 0; i < numThreads; ++i) {
        threads.emplace_back([&]() {
            // Perform read
            auto value = dict.TryGetValue(1);
            if (value.has_value()) {
                // Perform write
                dict.InsertOrUpdate(1, "updated");
                successfulOperations++;
            }
            });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    EXPECT_GT(successfulOperations, 0);
}

TEST_F(ConcurrentDictionaryTest, ConcurrentRemoveAndInsert) {
    const int numOperations = 1000;
    std::atomic<int> successfulRemovals{ 0 };
    std::atomic<int> successfulInsertions{ 0 };

    std::thread remover([&]() {
        for (int i = 0; i < numOperations; ++i) {
            if (dict.Remove(i)) {
                successfulRemovals++;
            }
        }
        });

    std::thread inserter([&]() {
        for (int i = 0; i < numOperations; ++i) {
            dict.InsertOrUpdate(i, std::to_string(i));
            successfulInsertions++;
        }
        });

    remover.join();
    inserter.join();

    EXPECT_GT(successfulInsertions, 0);
    EXPECT_LE(successfulRemovals + dict.ContainsKey(numOperations - 1), numOperations);
}

