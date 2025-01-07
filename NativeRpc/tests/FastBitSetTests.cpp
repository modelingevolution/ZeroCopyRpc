#include <gtest/gtest.h>

#include "FastBitSet.h"


// Test case for initialization
TEST(FastBitSetTest, Initialization) {
    EXPECT_NO_THROW(FastBitSet bitset(1));  // Small bitset
    EXPECT_NO_THROW(FastBitSet bitset(100)); // Larger bitset
    EXPECT_THROW(FastBitSet bitset(0), std::invalid_argument); // Invalid size
}

// Test case for bit setting and getting
TEST(FastBitSetTest, SetAndGetBit) {
    FastBitSet bitset(100);

    // Set some bits and verify
    bitset.setBit(0);
    bitset.setBit(50);
    bitset.setBit(99);

    EXPECT_TRUE(bitset.getBit(0));
    EXPECT_TRUE(bitset.getBit(50));
    EXPECT_TRUE(bitset.getBit(99));

    // Verify unset bits
    EXPECT_FALSE(bitset.getBit(1));
    EXPECT_FALSE(bitset.getBit(98));

    // Out-of-bounds access
    EXPECT_THROW(bitset.getBit(100), std::out_of_range);
    EXPECT_THROW(bitset.setBit(100), std::out_of_range);
}

// Test case for completeness
TEST(FastBitSetTest, IsComplete) {
    FastBitSet bitset(10);

    // Initially incomplete
    EXPECT_FALSE(bitset.isComplete());

    // Set all bits
    for (size_t i = 0; i < bitset.size(); ++i) {
        bitset.setBit(i);
    }

    // Verify completeness
    EXPECT_TRUE(bitset.isComplete());
}

// Test case for partial completeness
TEST(FastBitSetTest, PartialCompleteness) {
    FastBitSet bitset(5);

    // Set some bits
    bitset.setBit(0);
    bitset.setBit(2);

    // Still incomplete
    EXPECT_FALSE(bitset.isComplete());

    // Set remaining bits
    bitset.setBit(1);
    bitset.setBit(3);
    bitset.setBit(4);

    // Now complete
    EXPECT_TRUE(bitset.isComplete());
}

// Test case for edge cases with large bitsets
TEST(FastBitSetTest, LargeBitSet) {
    size_t size = 1024; // Large bitset
    FastBitSet bitset(size);

    // Set all bits
    for (size_t i = 0; i < size; ++i) {
        bitset.setBit(i);
    }

    // Verify completeness
    EXPECT_TRUE(bitset.isComplete());

    // Verify individual bits
    for (size_t i = 0; i < size; ++i) {
        EXPECT_TRUE(bitset.getBit(i));
    }
}

// Test case for boundary conditions
TEST(FastBitSetTest, BoundaryConditions) {
    FastBitSet bitset(65); // 1 full block + 1 extra bit

    // Verify the last bit can be set and read
    bitset.setBit(64);
    EXPECT_TRUE(bitset.getBit(64));

    // Verify the first bit can be set and read
    bitset.setBit(0);
    EXPECT_TRUE(bitset.getBit(0));
}

// Test case for padding
TEST(FastBitSetTest, PaddingBits) {
    FastBitSet bitset(70); // Non-multiple of 64

    // Set all bits
    for (size_t i = 0; i < bitset.size(); ++i) {
        bitset.setBit(i);
    }

    // Verify completeness
    EXPECT_TRUE(bitset.isComplete());
}

// Test case for edge cases with small bitsets
TEST(FastBitSetTest, SmallBitSet) {
    FastBitSet bitset(1); // Single bit

    // Initially incomplete
    EXPECT_FALSE(bitset.isComplete());

    // Set the single bit
    bitset.setBit(0);

    // Now complete
    EXPECT_TRUE(bitset.isComplete());
}
