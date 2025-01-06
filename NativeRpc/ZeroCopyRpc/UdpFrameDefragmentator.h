#pragma once
#include <memory>
#include <unordered_map>
#include "CyclicBuffer.hpp"
#include "UdpReplicationMessages.h"

class UdpFrameDefragmentator {
private:
    struct FrameState {
        CyclicBuffer::WriterScope scope;
        std::vector<bool> receivedChunks;
        uint64_t lastActivity;
        uint16_t receivedCount;
        uint16_t expectedChunks;

        FrameState(CyclicBuffer::WriterScope&& s, size_t chunks)
            : scope(std::move(s))
            , receivedChunks(chunks, false)
            , lastActivity(0)
            , receivedCount(0)
            , expectedChunks(chunks)
        {
        }

        bool isComplete() const {
            return receivedCount == expectedChunks;
        }
    };

    CyclicBuffer& buffer_;
    std::unique_ptr<FrameState> currentFrame_;
    std::unique_ptr<FrameState> previousFrame_;
    uint64_t currentFrameCreated_;
    uint64_t previousFrameCreated_;
    static constexpr uint16_t MAX_NEXT_FRAME_MESSAGES = 16;
    uint16_t nextFrameMessages_;

public:
    explicit UdpFrameDefragmentator(CyclicBuffer& buffer)
        : buffer_(buffer)
        , currentFrameCreated_(0)
        , previousFrameCreated_(0)
        , nextFrameMessages_(0)
    {
    }
    bool ProcessFragment(const uint8_t* msgData, size_t msgSize)
    {
        UdpReplicationMessageHeader* header = (UdpReplicationMessageHeader*)msgData;
        uint8_t* dataPtr = (uint8_t*)msgData + sizeof(UdpReplicationMessageHeader);
        size_t dataSize = msgSize - sizeof(UdpReplicationMessageHeader);
        return ProcessFragment(*header, dataPtr, dataSize);
    }

    bool ProcessFragment(const UdpReplicationMessageHeader& header, const uint8_t* data, size_t dataSize) {
        bool frameCompleted = false;

        if (header.Size == dataSize) {
            auto scope = buffer_.WriteScope(header.Size, header.Type);
            std::memcpy(scope.Span.Start, data, dataSize);
            scope.Span.Commit(header.Size);
            return true;
        }

        // Handle previous frame fragments
        if (previousFrame_ && header.Created == previousFrameCreated_) {
            frameCompleted = processFrameFragment(*previousFrame_, header, data, dataSize);
            if (frameCompleted) {
                previousFrame_.reset(); // Previous frame is complete, clean it up
            }
            return frameCompleted;
        }

        // Handle current frame fragments
        if (currentFrame_ && header.Created == currentFrameCreated_) {
            frameCompleted = processFrameFragment(*currentFrame_, header, data, dataSize);
            return frameCompleted;
        }

        // Handle new frame
        if (header.Created > currentFrameCreated_) {
            if (currentFrame_) {
                // Move current to previous if it exists
                previousFrame_ = std::move(currentFrame_);
                previousFrameCreated_ = currentFrameCreated_;
                nextFrameMessages_ = 1;
            }

            // Initialize new current frame
            initializeNewFrame(header, data, dataSize);
            return false;
        }

        if (currentFrame_ && header.Created > previousFrameCreated_) {
            nextFrameMessages_++;

            if (nextFrameMessages_ > MAX_NEXT_FRAME_MESSAGES && previousFrame_) {
                // Too many messages from newer frame and previous is still incomplete
                previousFrame_.reset();
            }
        }

        return false;
    }

private:
    void initializeNewFrame(const UdpReplicationMessageHeader& header, const uint8_t* data, size_t dataSize) {
        const size_t payloadSize = header.Size;
        auto scope = buffer_.WriteScope(payloadSize, header.Type);

        size_t numChunks = (payloadSize + dataSize - 1) / dataSize;
        currentFrame_ = std::make_unique<FrameState>(std::move(scope), numChunks);
        currentFrameCreated_ = header.Created;

        writeChunk(*currentFrame_, header, data, dataSize);
    }

    bool processFrameFragment(FrameState& frame, const UdpReplicationMessageHeader& header,
        const uint8_t* data, size_t dataSize) {
        if (frame.receivedChunks[header.Sequence]) {
            // Duplicate fragment - ignore
            return false;
        }

        writeChunk(frame, header, data, dataSize);

        if (frame.isComplete()) {
            frame.scope.Span.Commit(header.Size);
            return true;
        }

        return false;
    }

    void writeChunk(FrameState& frame, const UdpReplicationMessageHeader& header,
        const uint8_t* data, size_t dataSize) {
        const size_t offset = header.Sequence * dataSize;
        std::memcpy(frame.scope.Span.Start + offset, data, dataSize);

        frame.receivedChunks[header.Sequence] = true;
        frame.receivedCount++;
        frame.lastActivity = header.Created;
    }
};