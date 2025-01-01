#include <gtest/gtest.h>

#include <thread>
#include <chrono>

#include "CyclicBuffer.hpp"

const unsigned long BUFFER_SIZE = 1024;
const unsigned long CAPACITY = 16;

template class CyclicBuffer<1024, 16>;


class CyclicBufferTest : public ::testing::Test {
protected:

    CyclicBuffer<1024, 16> buffer;
};

// Test basic cursor chasing behavior
TEST_F(CyclicBufferTest, CursorChasing) {
    const unsigned long TYPE = 1;

    // Create a cursor before any writes
    auto cursor = buffer.ReadNext();
    ASSERT_EQ(cursor.Remaining(), 0);
    ASSERT_FALSE(cursor.TryRead());

    // Write data and verify cursor can read it
    int value1 = 42;
    {
        auto writer = buffer.WriteScope(sizeof(int), TYPE);
        memcpy(writer.Span.Start, &value1, sizeof(int));
        writer.Span.Commit(sizeof(int));
    }

    ASSERT_EQ(cursor.Remaining(), 1);
    ASSERT_TRUE(cursor.TryRead());
    ASSERT_EQ(*cursor.Data().As<int>(), value1);

    // Write more data and verify cursor follows
    int value2 = 43;
    {
        auto writer = buffer.WriteScope(sizeof(int), TYPE);
        memcpy(writer.Span.Start, &value2, sizeof(int));
        writer.Span.Commit(sizeof(int));
    }

    ASSERT_EQ(cursor.Remaining(), 1);
    ASSERT_TRUE(cursor.TryRead());
    ASSERT_EQ(*cursor.Data().As<int>(), value2);
}

// Test multiple cursors chasing independently
TEST_F(CyclicBufferTest, MultipleCursorsChasingIndependently) {
    const unsigned long TYPE = 1;

    // Create two cursors at different points
    auto cursor1 = buffer.ReadNext();

    // Write first value
    int value1 = 42;
    {
        auto writer = buffer.WriteScope(sizeof(int), TYPE);
        memcpy(writer.Span.Start, &value1, sizeof(int));
        writer.Span.Commit(sizeof(int));
    }

    // Create second cursor after first write
    auto cursor2 = buffer.ReadNext();

    // Write second value
    int value2 = 43;
    {
        auto writer = buffer.WriteScope(sizeof(int), TYPE);
        memcpy(writer.Span.Start, &value2, sizeof(int));
        writer.Span.Commit(sizeof(int));
    }

    // Verify cursor1 sees both values
    ASSERT_EQ(cursor1.Remaining(), 2);
    ASSERT_TRUE(cursor1.TryRead());
    ASSERT_EQ(*cursor1.Data().As<int>(), value1);
    ASSERT_TRUE(cursor1.TryRead());
    ASSERT_EQ(*cursor1.Data().As<int>(), value2);

    // Verify cursor2 only sees the second value
    ASSERT_EQ(cursor2.Remaining(), 1);
    ASSERT_TRUE(cursor2.TryRead());
    ASSERT_EQ(*cursor2.Data().As<int>(), value2);
}

// Test cursor chasing with wrap-around
TEST_F(CyclicBufferTest, CursorChasingWithWrapAround) {
    const unsigned long TYPE = 1;
    auto cursor = buffer.ReadNext();

    // Fill buffer to capacity
    for (int i = 0; i < CAPACITY; i++) {
        auto writer = buffer.WriteScope(sizeof(int), TYPE);
        memcpy(writer.Span.Start, &i, sizeof(int));
        writer.Span.Commit(sizeof(int));
    }

    // Read all values
    for (int i = 0; i < CAPACITY; i++) {
        ASSERT_TRUE(cursor.TryRead());
        ASSERT_EQ(*cursor.Data().As<int>(), i);
    }

    // Write and read after wrap-around
    int newValue = 999;
    {
        auto writer = buffer.WriteScope(sizeof(int), TYPE);
        memcpy(writer.Span.Start, &newValue, sizeof(int));
        writer.Span.Commit(sizeof(int));
    }

    ASSERT_TRUE(cursor.TryRead());
    ASSERT_EQ(*cursor.Data().As<int>(), newValue);
}

// Test reading from specific index with chasing
TEST_F(CyclicBufferTest, ReadFromIndexWithChasing) {
    const unsigned long TYPE = 1;

    // Write initial value
    int value1 = 42;
    {
        auto writer = buffer.WriteScope(sizeof(int), TYPE);
        memcpy(writer.Span.Start, &value1, sizeof(int));
        writer.Span.Commit(sizeof(int));
    }

    // Create cursor at current index
    auto currentIndex = buffer.NextIndex();
    auto cursor = buffer.ReadNext(currentIndex - 1);

    // Verify initial value
    ASSERT_TRUE(cursor.TryRead());
    ASSERT_EQ(*cursor.Data().As<int>(), value1);

    // Write new value and verify cursor chases
    int value2 = 43;
    {
        auto writer = buffer.WriteScope(sizeof(int), TYPE);
        memcpy(writer.Span.Start, &value2, sizeof(int));
        writer.Span.Commit(sizeof(int));
    }

    ASSERT_TRUE(cursor.TryRead());
    ASSERT_EQ(*cursor.Data().As<int>(), value2);
}

// Test async writing with cursor chasing
TEST_F(CyclicBufferTest, AsyncWritingWithChasing) {
    const unsigned long TYPE = 1;
    const int NUM_WRITES = 5;

    auto cursor = buffer.ReadNext();

    // Simulate async writing in another thread
    std::thread writer([this]() {
        for (int i = 0; i < NUM_WRITES; i++) {
            buffer.Write<int>(TYPE,i);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        });

    // Read values as they become available
    for (int i = 0; i < NUM_WRITES; i++) {
        while (!cursor.TryRead()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        ASSERT_EQ(*cursor.Data().As<int>(), i);
    }

    writer.join();
}

// Test zero-sized writes with cursor
TEST_F(CyclicBufferTest, ZeroSizedWritesWithCursor) {
    const unsigned long TYPE = 1;
    auto cursor = buffer.ReadNext();

    // Zero-sized write shouldn't affect cursor
    {
        auto writer = buffer.WriteScope(0, TYPE);
        // Don't commit anything
    }

    ASSERT_EQ(cursor.Remaining(), 0);
    ASSERT_FALSE(cursor.TryRead());

    // Write actual data
    int value = 42;
    buffer.Write<int>(TYPE, value);

    ASSERT_EQ(cursor.Remaining(), 1);
    ASSERT_TRUE(cursor.TryRead());
    ASSERT_EQ(*cursor.Data().As<int>(), value);
}

TEST_F(CyclicBufferTest, RemainingBasicBehavior) {
    const unsigned long TYPE = 1;
    auto cursor = buffer.ReadNext();

    // Initially should have zero remaining
    ASSERT_EQ(cursor.Remaining(), 0);

    // Write one item
    int value = 42;
    buffer.Write<int>(TYPE, value);

    // Should show one remaining
    ASSERT_EQ(cursor.Remaining(), 1);

    // After reading, should show zero remaining
    ASSERT_TRUE(cursor.TryRead());
    ASSERT_EQ(cursor.Remaining(), 0);
}

TEST_F(CyclicBufferTest, RemainingMultipleWrites) {
    const unsigned long TYPE = 1;
    auto cursor = buffer.ReadNext();

    // Write multiple items
    const int NUM_ITEMS = 5;
    for (int i = 0; i < NUM_ITEMS; i++) {
        buffer.Write<int>(TYPE, i);
    }

    // Should show all items as remaining
    ASSERT_EQ(cursor.Remaining(), NUM_ITEMS);

    // Read half the items
    for (int i = 0; i < NUM_ITEMS / 2; i++) {
        ASSERT_TRUE(cursor.TryRead());
    }

    // Should show remaining half
    ASSERT_EQ(cursor.Remaining(), NUM_ITEMS - (NUM_ITEMS / 2));
}

TEST_F(CyclicBufferTest, RemainingWithMultipleCursors) {
    const unsigned long TYPE = 1;

    // Write first batch
    const int FIRST_BATCH = 3;
    for (int i = 0; i < FIRST_BATCH; i++) {
        buffer.Write<int>(TYPE, i);
    }

    // Create first cursor - it will start at current write position
    auto cursor1 = buffer.ReadNext();
    ASSERT_EQ(cursor1.Remaining(), 0);  // Should be 0 as cursor starts at current write position

    // Create second cursor at the same point
    auto cursor2 = buffer.ReadNext();
    ASSERT_EQ(cursor2.Remaining(), 0);  // Should also be 0

    // Write second batch - now both cursors should see these new items
    const int SECOND_BATCH = 2;
    for (int i = 0; i < SECOND_BATCH; i++) {
        buffer.Write<int>(TYPE, i);
    }

    // Both cursors should see the new writes
    ASSERT_EQ(cursor1.Remaining(), SECOND_BATCH);
    ASSERT_EQ(cursor2.Remaining(), SECOND_BATCH);

    // Read one item from cursor1
    ASSERT_TRUE(cursor1.TryRead());
    ASSERT_EQ(cursor1.Remaining(), SECOND_BATCH - 1);  // One less for cursor1
    ASSERT_EQ(cursor2.Remaining(), SECOND_BATCH);      // cursor2 hasn't read anything

    // Write third batch
    const int THIRD_BATCH = 1;
    buffer.Write<int>(THIRD_BATCH);

    // Check remaining counts after new write
    ASSERT_EQ(cursor1.Remaining(), (SECOND_BATCH - 1) + THIRD_BATCH);
    ASSERT_EQ(cursor2.Remaining(), SECOND_BATCH + THIRD_BATCH);

    // Read all items from cursor2
    for (int i = 0; i < (SECOND_BATCH + THIRD_BATCH); i++) {
        ASSERT_TRUE(cursor2.TryRead());
    }
    ASSERT_EQ(cursor2.Remaining(), 0);

    // cursor1 should still have its remaining items
    ASSERT_EQ(cursor1.Remaining(), (SECOND_BATCH - 1) + THIRD_BATCH);
}

TEST_F(CyclicBufferTest, RemainingWithWrapAround) {
    const unsigned long TYPE = 1;
    auto cursor = buffer.ReadNext();

    // Fill buffer to capacity
    for (int i = 0; i < CAPACITY; i++) {
        buffer.Write<int>(TYPE, i);
    }

    ASSERT_EQ(cursor.Remaining(), CAPACITY);

    // Read half
    const int READ_COUNT = CAPACITY / 2;
    for (int i = 0; i < READ_COUNT; i++) {
        ASSERT_TRUE(cursor.TryRead());
    }

    ASSERT_EQ(cursor.Remaining(),CAPACITY - READ_COUNT);

    // Write more data causing wrap-around
    const int ADDITIONAL_WRITES = 3;
    for (int i = 0; i < ADDITIONAL_WRITES; i++) {
        buffer.Write<int>(TYPE, i);
    }

    // Should show remaining unread items plus new items
    ASSERT_EQ(cursor.Remaining(), (CAPACITY - READ_COUNT) + ADDITIONAL_WRITES);
}

TEST_F(CyclicBufferTest, RemainingAfterZeroSizedWrites) {
    const unsigned long TYPE = 1;
    auto cursor = buffer.ReadNext();

    // Zero-sized write
    {
        auto writer = buffer.WriteScope(0, TYPE);
        // Don't commit anything
    }

    // Should still show zero remaining
    ASSERT_EQ(cursor.Remaining(), 0);

    // Write actual data
    int value = 42;
    buffer.Write<int>(TYPE, value);
    

    // Should show one remaining
    ASSERT_EQ(cursor.Remaining(), 1);
}

TEST_F(CyclicBufferTest, RemainingWithSpecificStartIndex) {
    const unsigned long TYPE = 1;

    // Write some initial data
    const int INITIAL_WRITES = 3;
    for (int i = 0; i < INITIAL_WRITES; i++) {
        buffer.Write<int>(TYPE, i);
    }

    // Create cursor at second item
    auto cursor = buffer.ReadNext(1);  // Start from index 1
    ASSERT_EQ(cursor.Remaining(), INITIAL_WRITES - 1);

    // Write more data
    const int ADDITIONAL_WRITES = 2;
    for (int i = 0; i < ADDITIONAL_WRITES; i++) {
        buffer.Write<int>(TYPE, i);
    }

    // Should show remaining from start point plus new items
    ASSERT_EQ(cursor.Remaining(), (INITIAL_WRITES - 1) + ADDITIONAL_WRITES);
}