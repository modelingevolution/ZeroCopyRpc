#include <gtest/gtest.h>
#include "UdpFrameProcessor.h"
constexpr size_t TEST_UDP_MTU = 1500;

TEST(UdpFrameIteratorTest, Initialization) {
    uint8_t buffer[1024] = { 0 };
    uint8_t type = 1;
    uint64_t created = 123456789;
    UdpFrameIterator<TEST_UDP_MTU> iterator(buffer, sizeof(buffer), type, created);
    EXPECT_TRUE(iterator.CanRead());
}
TEST(UdpFrameIteratorTest, Dereference) {
    uint8_t buffer[1024] = { 0 };
    uint8_t type = 1;
    uint64_t created = 123456789;
    UdpFrameIterator<TEST_UDP_MTU> iterator(buffer, sizeof(buffer), type, created);
    auto fragment = *iterator;
    // Verify header
    const UdpReplicationMessageHeader* header = static_cast<const UdpReplicationMessageHeader*>(fragment[0].data());
    EXPECT_EQ(header->Size, sizeof(buffer));
    EXPECT_EQ(header->Type, type);
    EXPECT_EQ(header->Created, created);
    EXPECT_EQ(header->Sequence, 0);
    // Verify payload
    EXPECT_EQ(fragment[1].size(), std::min(TEST_UDP_MTU - sizeof(UdpReplicationMessageHeader), sizeof(buffer)));
}
TEST(UdpFrameIteratorTest, Increment) {
    uint8_t buffer[3000] = { 0 }; // Larger than MTU
    uint8_t type = 1;
    uint64_t created = 123456789;
    UdpFrameIterator<TEST_UDP_MTU> iterator(buffer, sizeof(buffer), type, created);
    size_t expectedOffset = 0;
    uint16_t expectedSequence = 0;
    while (iterator.CanRead()) {
        auto fragment = *iterator;
        // Verify header
        const UdpReplicationMessageHeader* header = static_cast<const UdpReplicationMessageHeader*>(fragment[0].data());
        EXPECT_EQ(header->Sequence, expectedSequence);
        // Verify offset
        EXPECT_EQ(fragment[1].data(), buffer + expectedOffset);
        // Increment
        ++iterator;
        expectedOffset += TEST_UDP_MTU - sizeof(UdpReplicationMessageHeader);
        ++expectedSequence;
    }
    EXPECT_EQ(expectedSequence, 3);
    EXPECT_FALSE(iterator.CanRead());
}
TEST(UdpFrameIteratorTest, EndCondition) {
    uint8_t buffer[1024] = { 0 };
    uint8_t type = 1;
    uint64_t created = 123456789;
    auto begin = UdpFrameIterator<TEST_UDP_MTU>::Begin(buffer, sizeof(buffer), type, created);
    auto end = UdpFrameIterator<TEST_UDP_MTU>::End(buffer, sizeof(buffer), type, created);
    EXPECT_NE(begin, end);
    while (begin != end) {
        ++begin;
    }
    EXPECT_EQ(begin, end);
}
TEST(UdpFrameIteratorTest, OutOfRangeAccess) {
    uint8_t buffer[1024] = { 0 };
    uint8_t type = 1;
    uint64_t created = 123456789;
    UdpFrameIterator<TEST_UDP_MTU> iterator(buffer, sizeof(buffer), type, created);
    while (iterator.CanRead()) {
        ++iterator;
    }
    EXPECT_THROW(*iterator, std::out_of_range);
    EXPECT_THROW(++iterator, std::out_of_range);
}
