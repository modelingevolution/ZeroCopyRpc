#include <gtest/gtest.h>
#include "UdpFrameDefragmentator.h" // Assuming this is the header file for your class
#include "CyclicBuffer.hpp"

class UdpFrameDefragmentatorTest : public ::testing::Test {
protected:
    const unsigned long BUFFER_CAPACITY = 1024;
    const unsigned long BUFFER_SIZE = 65536; // 64 KB

    CyclicBuffer cyclicBuffer;
    std::unique_ptr<UdpFrameDefragmentator> defragmentator;

    UdpFrameDefragmentatorTest()
        : cyclicBuffer(BUFFER_CAPACITY, BUFFER_SIZE),
        defragmentator(std::make_unique<UdpFrameDefragmentator>(cyclicBuffer)) {
    }

    void SetUp() override {
        // Additional setup if required
    }

    void TearDown() override {
        // Cleanup if necessary
    }
};

std::vector<byte> CreateFragment(uint64_t created, uint32_t size, uint16_t sequence, uint8_t type, const std::vector<byte>& data) {
    std::vector<byte> fragment;
    fragment.resize(sizeof(UdpReplicationMessageHeader) + data.size());

    // Write header
    UdpReplicationMessageHeader header(created, size, sequence, type);
    std::memcpy(fragment.data(), &header, sizeof(header));

    // Append data
    std::memcpy(fragment.data() + sizeof(header), data.data(), data.size());

    return fragment;
}
TEST_F(UdpFrameDefragmentatorTest, SingleFragmentFrame) {
    // Prepare a single-frame message
    uint64_t created = 1234567890; // Example timestamp
    uint32_t size = 7;            // Payload size
    uint16_t sequence = 0;        // Frame sequence
    uint8_t type = 0;             // Message type
    std::vector<byte> data = { 'H', 'e', 'l', 'l', 'o', '!', '\0' };
    auto fragment = CreateFragment(created, size, sequence, type, data);

    auto cursor = cyclicBuffer.OpenCursor();

    // Process the fragment
    auto finished = defragmentator->ProcessFragment(fragment.data(), fragment.size());

    ASSERT_TRUE(finished);

    // Validate the reassembled frame
    ASSERT_TRUE(cursor.TryRead());
    auto accessor = cursor.Data();

    // Check for correctness
    byte* reassembledData = accessor.Get();
    uint32_t reassembledDataSize = accessor.Size();
    auto reassembledVector = std::vector<byte>(reassembledData, reassembledData + reassembledDataSize);
    ASSERT_EQ(data, reassembledVector);
    ASSERT_EQ(data.size(), accessor.Size());
}

TEST_F(UdpFrameDefragmentatorTest, MultipleFragmentFrame) {
    // Simulate sending multiple fragments
    uint64_t created = 1234567890; // Example timestamp
    uint32_t totalSize = 13;       // Total payload size
    uint16_t sequence = 0;         // Frame sequence
    uint8_t type = 0;              // Message type

    std::vector<byte> part1 = { 'H', 'e', 'l' };
    std::vector<byte> part2 = { 'l', 'o', ',', ' ' };
    std::vector<byte> part3 = { 'W', 'o', 'r', 'l', 'd', '!', '\0' };

    auto fragment1 = CreateFragment(created, totalSize, sequence++, type, part1);
    auto fragment2 = CreateFragment(created, totalSize, sequence++, type, part2);
    auto fragment3 = CreateFragment(created, totalSize, sequence++, type, part3);

    auto cursor = cyclicBuffer.OpenCursor();

    // Emulate sending fragments
    defragmentator->ProcessFragment(fragment1.data(), fragment1.size());
    defragmentator->ProcessFragment(fragment2.data(), fragment2.size());
    defragmentator->ProcessFragment(fragment3.data(), fragment3.size());

    // Validate the reassembled frame
    
    ASSERT_TRUE(cursor.TryRead());
    auto accessor = cursor.Data();

    // Check for correctness
    std::vector<byte> expectedData = { 'H', 'e', 'l', 'l', 'o', ',', ' ', 'W', 'o', 'r', 'l', 'd', '!', '\0' };
    auto* reassembledData = accessor.As<std::vector<byte>>();
    ASSERT_EQ(expectedData, *reassembledData);
    ASSERT_EQ(expectedData.size(), accessor.Size());
}