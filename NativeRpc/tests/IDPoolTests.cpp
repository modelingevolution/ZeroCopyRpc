#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <set>
#include "IDPool.hpp"

class IDPoolTest : public ::testing::Test {
protected:
    IDPool<uint8_t, 256> pool;
    IDPool<uint8_t, 255> pool_small;
    IDPool<uint8_t, 2> pool_with_2_free_items;
};

TEST_F(IDPoolTest, InitialRentReturnsSequentialIDs) {
    uint8_t id;
    ASSERT_TRUE(pool.rent(id));
    EXPECT_EQ(id, 0);
    ASSERT_TRUE(pool.rent(id));
    EXPECT_EQ(id, 1);
}

TEST_F(IDPoolTest, ReturnMakesIDAvailableAgain) {
    uint8_t id1;
    ASSERT_TRUE(pool.rent(id1));
    pool.returns(id1);

    uint8_t id2;
    ASSERT_TRUE(pool.rent(id2));
    EXPECT_EQ(id1, id2);
}

TEST_F(IDPoolTest, RentSpecificID) {
    ASSERT_TRUE(pool.try_rent(42));
    ASSERT_FALSE(pool.try_rent(42));
    pool.returns(42);
    ASSERT_TRUE(pool.try_rent(42));
}

TEST_F(IDPoolTest, OutOfRangeReturn) {
    EXPECT_THROW(pool_small.returns(255), std::out_of_range);
}

TEST_F(IDPoolTest, DoubleReturn) {
    uint8_t id;
    ASSERT_TRUE(pool.rent(id));
    pool.returns(id);
    EXPECT_THROW(pool.returns(id), std::runtime_error);
}

TEST_F(IDPoolTest, ExhaustPool) {
    std::vector<uint8_t> ids;
    uint8_t id;

    for (int i = 0; i < 256; i++) {
        ASSERT_TRUE(pool.rent(id));
        ids.push_back(id);
    }

    ASSERT_FALSE(pool.rent(id));
    EXPECT_TRUE(pool.empty());

    for (auto returned_id : ids) {
        pool.returns(returned_id);
    }
    EXPECT_FALSE(pool.empty());
}

TEST_F(IDPoolTest, ConcurrentRentReturns) {
    constexpr int NUM_THREADS = 4;
    constexpr int OPS_PER_THREAD = 20;

    auto worker = [&]() {
        std::set<uint8_t> thread_ids;
        for (int i = 0; i < OPS_PER_THREAD; i++) {
            uint8_t id;
            if (pool.rent(id)) {
                EXPECT_TRUE(thread_ids.insert(id).second);
                std::this_thread::yield();
                pool.returns(id);
                thread_ids.erase(id);
            }
        }
        };

    std::vector<std::thread> threads;
    for (int i = 0; i < NUM_THREADS; i++) {
        threads.emplace_back(worker);
    }

    for (auto& t : threads) {
        t.join();
    }

    // Verify pool state after concurrent operations
    std::set<uint8_t> remaining_ids;
    uint8_t id;
    while (pool.rent(id)) {
        EXPECT_TRUE(remaining_ids.insert(id).second);
        //pool.returns(id);
    }
    EXPECT_EQ(remaining_ids.size(), 256);
}

TEST_F(IDPoolTest, RentTwoItemsSuccessfully) {
    uint8_t id1, id2;
    ASSERT_TRUE(pool_with_2_free_items.rent(id1));
    ASSERT_TRUE(pool_with_2_free_items.rent(id2));
    EXPECT_NE(id1, id2);
}

TEST_F(IDPoolTest, CannotRentMoreThanTwo) {
    uint8_t id1, id2, id3;
    ASSERT_TRUE(pool_with_2_free_items.rent(id1));
    ASSERT_TRUE(pool_with_2_free_items.rent(id2));
    ASSERT_FALSE(pool_with_2_free_items.rent(id3));
}

TEST_F(IDPoolTest, ReturnAndRentAgain) {
    uint8_t id1, id2, id3;
    ASSERT_TRUE(pool_with_2_free_items.rent(id1));
    ASSERT_TRUE(pool_with_2_free_items.rent(id2));
    ASSERT_FALSE(pool_with_2_free_items.rent(id3));

    pool_with_2_free_items.returns(id1);
    ASSERT_TRUE(pool_with_2_free_items.rent(id3));
    EXPECT_EQ(id1, id3);
}

TEST_F(IDPoolTest, TryRentSpecificWithFullPool) {
    uint8_t id1, id2;
    ASSERT_TRUE(pool_with_2_free_items.rent(id1));
    ASSERT_TRUE(pool_with_2_free_items.rent(id2));
    ASSERT_FALSE(pool_with_2_free_items.try_rent(0));
}

TEST_F(IDPoolTest, EmptyPoolAfterTwoRents) {
    uint8_t id1, id2;
    ASSERT_FALSE(pool_with_2_free_items.empty());
    ASSERT_TRUE(pool_with_2_free_items.rent(id1));
    ASSERT_FALSE(pool_with_2_free_items.empty());
    ASSERT_TRUE(pool_with_2_free_items.rent(id2));
    ASSERT_TRUE(pool_with_2_free_items.empty());
}

TEST_F(IDPoolTest, ConcurrentSpecificIDs) {
    constexpr int NUM_THREADS = 4;
    std::atomic<int> successful_rents{ 0 };

    auto worker = [&](uint8_t target_id) {
        if (pool.try_rent(target_id)) {
            successful_rents++;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            pool.returns(target_id);
        }
        };

    std::vector<std::thread> threads;
    for (int i = 0; i < NUM_THREADS; i++) {
        threads.emplace_back(worker, 42);
    }

    for (auto& t : threads) {
        t.join();
    }

    EXPECT_GT(successful_rents, 0);
    EXPECT_TRUE(pool.try_rent(42));
}