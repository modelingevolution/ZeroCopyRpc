#include <gtest/gtest.h>
#include <chrono>
#include <random>
#include <sstream>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/detail/md5.hpp>

#include "BigFrame.hpp"
#include "UdpFrameDefragmentator.h"
#include "CyclicBuffer.hpp"

using namespace boost::uuids;

class UdpFrameDefragmentatorPerfTest : public ::testing::Test {
protected:
    static constexpr unsigned long BUFFER_CAPACITY = 16384; // Increased for larger messages
    static constexpr unsigned long BUFFER_SIZE = 8 * 1024 * 1024; // 8MB to handle large messages

    CyclicBuffer cyclicBuffer;
    std::unique_ptr<UdpFrameDefragmentator> defragmentator;
    size_t mtu;

    UdpFrameDefragmentatorPerfTest()
        : cyclicBuffer(BUFFER_CAPACITY, BUFFER_SIZE)
    {
    }

    void SetUp(size_t mtuSize) {
        mtu = mtuSize;
        defragmentator = std::make_unique<UdpFrameDefragmentator>(cyclicBuffer, mtuSize);
    }
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
    template<size_t Size>
    std::vector<std::vector<byte>> FragmentBigFrame(const BigFrame<Size>& frame, uint64_t created, uint8_t type) {
        std::vector<std::vector<byte>> fragments;
        const size_t maxPayloadSize = mtu - sizeof(UdpReplicationMessageHeader);
        const size_t totalSize = Size + sizeof(uuid); // Data + Hash
        const size_t numFragments = (totalSize + maxPayloadSize - 1) / maxPayloadSize;

        // Prepare buffer containing hash and data
        std::vector<byte> fullData(totalSize);
        memcpy(fullData.data(), &frame.Hash, sizeof(uuid));
        memcpy(fullData.data() + sizeof(uuid), frame.Data, Size);

        for (size_t i = 0; i < numFragments; ++i) {
            size_t offset = i * maxPayloadSize;
            size_t remainingSize = totalSize - offset;
            size_t fragmentDataSize = std::min<size_t>(maxPayloadSize, remainingSize);

            std::vector<byte> fragmentData(fragmentDataSize);
            memcpy(fragmentData.data(), fullData.data() + offset, fragmentDataSize);

            fragments.push_back(CreateFragment(created, totalSize, i, type, fragmentData));
        }

        return fragments;
    }

    template<size_t Size>
    void TestBigFrameTransfer(const char* testName) {
        using namespace std::chrono;

        auto bigFrame = std::make_unique<BigFrame<Size>>();
        BigFrame<Size>& frame = *bigFrame;
        uint64_t created = 1234567890;
        uint8_t type = 1;

        auto cursor = cyclicBuffer.OpenCursor();
        auto fragments = FragmentBigFrame<Size>(frame, created, type);

        // Measure defragmentation time
        auto start = high_resolution_clock::now();

        for (const auto& fragment : fragments) {
            defragmentator->ProcessFragment(fragment.data(), fragment.size());
        }

        auto end = high_resolution_clock::now();
        auto duration = duration_cast<microseconds>(end - start);
        double totalMilliseconds = std::chrono::duration<double, std::milli>(end - start).count();
        // Verify the result
        ASSERT_TRUE(cursor.TryRead());
        auto accessor = cursor.Data();

        // Check size
        ASSERT_EQ(Size + sizeof(uuid), accessor.Size());

        // Verify hash
        uuid receivedHash;
        memcpy(&receivedHash, accessor.Get(), sizeof(uuid));
        ASSERT_EQ(frame.Hash, receivedHash);

        // Verify data
        ASSERT_EQ(0, memcmp(frame.Data, accessor.Get() + sizeof(uuid), Size));

        std::cout << testName << " Stats:" << std::endl
            << "  MTU: " << mtu << " bytes" << std::endl
            << "  Fragments: " << fragments.size() << std::endl
            << "  Total time: " << duration << std::endl
            << "  Throughput: " << (Size / 1024.0 / 1024.0) / (totalMilliseconds / 1000.0) << " MB/s" << std::endl;
    }
};

TEST_F(UdpFrameDefragmentatorPerfTest, Test100KB_StandardMTU) {
    SetUp(1500);
    TestBigFrameTransfer<102400>("100KB Transfer (Standard MTU)");
}

TEST_F(UdpFrameDefragmentatorPerfTest, Test100KB_JumboMTU) {
    SetUp(9000);
    TestBigFrameTransfer<102400>("100KB Transfer (Jumbo MTU)");
}

TEST_F(UdpFrameDefragmentatorPerfTest, Test1MB_StandardMTU) {
    SetUp(1500);
    TestBigFrameTransfer<1048576>("1MB Transfer (Standard MTU)");
}

TEST_F(UdpFrameDefragmentatorPerfTest, Test1MB_JumboMTU) {
    SetUp(9000);
    TestBigFrameTransfer<1048576>("1MB Transfer (Jumbo MTU)");
}

TEST_F(UdpFrameDefragmentatorPerfTest, Test4MB_StandardMTU) {
    SetUp(1500);
    TestBigFrameTransfer<4194304>("4MB Transfer (Standard MTU)");
}

TEST_F(UdpFrameDefragmentatorPerfTest, Test4MB_JumboMTU) {
    SetUp(9000);
    TestBigFrameTransfer<4194304>("4MB Transfer (Jumbo MTU)");
}

// Test out-of-order delivery
TEST_F(UdpFrameDefragmentatorPerfTest, Test1MB_OutOfOrder) {
    SetUp(1500);

    auto bigFrame = std::make_unique<BigFrame<1048576>>();
    BigFrame<1048576>& frame = *bigFrame;
    uint64_t created = 1234567890;
    uint8_t type = 1;

    auto cursor = cyclicBuffer.OpenCursor();
    auto fragments = FragmentBigFrame<1048576>(frame, created, type);

    // Shuffle fragments to simulate out-of-order delivery
    std::random_device rd;
    std::mt19937 gen(rd());
    std::shuffle(fragments.begin(), fragments.end(), gen);

    for (const auto& fragment : fragments) {
        defragmentator->ProcessFragment(fragment.data(), fragment.size());
    }

    ASSERT_TRUE(cursor.TryRead());
    auto accessor = cursor.Data();

    // Verify hash
    boost::uuids::uuid receivedHash;
    memcpy(&receivedHash, accessor.Get(), sizeof(boost::uuids::uuid));
    ASSERT_EQ(frame.Hash, receivedHash);
}