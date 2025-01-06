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
    const size_t mtu_;
    const size_t maxPayloadSize_; // MTU - header size

public:
    explicit UdpFrameDefragmentator(CyclicBuffer& buffer, size_t mtu)
        : buffer_(buffer)
        , currentFrameCreated_(0)
        , previousFrameCreated_(0)
        , nextFrameMessages_(0)
        , mtu_(mtu)
        , maxPayloadSize_(mtu - sizeof(UdpReplicationMessageHeader))
    {
    }

    bool ProcessFragment(const uint8_t* msgData, size_t msgSize) {
        UdpReplicationMessageHeader* header = (UdpReplicationMessageHeader*)msgData;
        uint8_t* dataPtr = (uint8_t*)msgData + sizeof(UdpReplicationMessageHeader);
        size_t dataSize = msgSize - sizeof(UdpReplicationMessageHeader);
        return ProcessFragment(*header, dataPtr, dataSize);
    }

    bool ProcessFragment(const UdpReplicationMessageHeader& header, const uint8_t* data, size_t dataSize) {
        // If header.Size equals dataSize, we have a complete message
        if (header.Size == dataSize) {
            auto scope = buffer_.WriteScope(header.Size, header.Type);
            std::memcpy(scope.Span.Start, data, dataSize);
            scope.Span.Commit(header.Size);
            return true;
        }

        // Handle previous frame fragments
        if (previousFrame_ && header.Created == previousFrameCreated_) {
            bool frameCompleted = processFrameFragment(*previousFrame_, header, data, dataSize);
            if (frameCompleted) {
                previousFrame_.reset();
            }
            return frameCompleted;
        }

        // Handle current frame fragments
        if (currentFrame_ && header.Created == currentFrameCreated_) {
            return processFrameFragment(*currentFrame_, header, data, dataSize);
        }

        // Handle new frame
        if (header.Created > currentFrameCreated_) {
            if (currentFrame_) {
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
                previousFrame_.reset();
            }
        }

        return false;
    }

private:
    void initializeNewFrame(const UdpReplicationMessageHeader& header, const uint8_t* data, size_t dataSize) {
        auto scope = buffer_.WriteScope(header.Size, header.Type);

        // Calculate number of chunks based on max payload size
        size_t numChunks = (header.Size + maxPayloadSize_ - 1) / maxPayloadSize_;
        currentFrame_ = std::make_unique<FrameState>(std::move(scope), numChunks);
        currentFrameCreated_ = header.Created;

        writeChunk(*currentFrame_, header, data, dataSize);
    }

    bool processFrameFragment(FrameState& frame, const UdpReplicationMessageHeader& header,
        const uint8_t* data, size_t dataSize) {
        if (frame.receivedChunks[header.Sequence]) {
            return false;
        }

        writeChunk(frame, header, data, dataSize);

        if (frame.isComplete()) {
            frame.scope.Span.Commit(header.Size);
            this->currentFrame_.reset();
            return true;
        }

        return false;
    }

    void writeChunk(FrameState& frame, const UdpReplicationMessageHeader& header,
        const uint8_t* data, size_t dataSize) {
        // Calculate offset based on MTU payload size
        const size_t offset = header.Sequence * maxPayloadSize_;
        std::memcpy(frame.scope.Span.Start + offset, data, dataSize);

        frame.receivedChunks[header.Sequence] = true;
        frame.receivedCount++;
        frame.lastActivity = header.Created;
    }
};