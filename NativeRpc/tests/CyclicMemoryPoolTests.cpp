#include <gtest/gtest.h>
#include <thread>
#include "CyclicMemoryPool.hpp"

class CyclicMemoryPoolTest : public ::testing::Test {
protected:
    const size_t DEFAULT_SIZE = 1024;
    CyclicMemoryPool* pool;

    void SetUp() override {
        pool = new CyclicMemoryPool(DEFAULT_SIZE);
    }

    void TearDown() override {
        delete pool;
    }
};

TEST_F(CyclicMemoryPoolTest, InternalBufferConstruction) {
    EXPECT_EQ(pool->Size(), DEFAULT_SIZE);
    EXPECT_NE(pool->Get(0), nullptr);
}

TEST_F(CyclicMemoryPoolTest, ExternalBufferConstruction) {
    byte* buffer = new byte[CyclicMemoryPool::SizeOf(DEFAULT_SIZE)];
    CyclicMemoryPool* extPool = new CyclicMemoryPool(buffer, DEFAULT_SIZE);

    EXPECT_EQ(extPool->Size(), DEFAULT_SIZE);
    EXPECT_NE(extPool->Get(0), nullptr);

    // Check if we don't have seg-fault.
    unsigned short* latestValue;
    for(unsigned short i = 0 ; i < DEFAULT_SIZE*2; i++)
    {
        latestValue = extPool->Write<unsigned short>(i);
    }
    
    EXPECT_EQ(*latestValue, DEFAULT_SIZE * 2 - 1);

    delete extPool;
    delete[] buffer;
}

TEST_F(CyclicMemoryPoolTest, SpanOperations) {
    auto span = pool->GetWriteSpan(100);
    EXPECT_EQ(span.CommitedSize(), 0);

    span.Commit(50);
    EXPECT_EQ(span.CommitedSize(), 50);
    EXPECT_EQ(span.End(), span.Start + 50);

    span.Commit(30);
    EXPECT_EQ(span.CommitedSize(), 80);
}

TEST_F(CyclicMemoryPoolTest, SpanMoveSemantics) {
    auto span1 = pool->GetWriteSpan(100);
    span1.Commit(50);

    auto span2 = std::move(span1);
    EXPECT_EQ(span1.Start, nullptr);
    EXPECT_EQ(span2.CommitedSize(), 50);
}



TEST_F(CyclicMemoryPoolTest, BufferWrapping) {
    {
        auto span1 = pool->GetWriteSpan(DEFAULT_SIZE - 50);
        span1.Commit(DEFAULT_SIZE - 50);
    }
    auto span2 = pool->GetWriteSpan(100);
    EXPECT_EQ(span2.StartOffset(), 0);
}

TEST_F(CyclicMemoryPoolTest, ErrorCases) {
    EXPECT_THROW(pool->GetWriteSpan(DEFAULT_SIZE + 1), std::runtime_error);

    auto span = pool->GetWriteSpan(100);
    EXPECT_THROW(span.Commit(DEFAULT_SIZE+1), std::runtime_error);
    
}