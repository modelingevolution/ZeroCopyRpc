#pragma once
#include <string>
#include <memory>
#include "CyclicBuffer.hpp"
#include <chrono>

class EXPORT ISubscriptionCursor {
public:
    virtual ~ISubscriptionCursor() = default;

    // Try reading data for a specific duration.
    virtual bool TryReadFor(CyclicBuffer::Accessor& accessor, const std::chrono::milliseconds& timeout) = 0;

    // Try reading data without a timeout.
    virtual bool TryRead(CyclicBuffer::Accessor& accessor) = 0;
    virtual CyclicBuffer::Accessor Read() = 0;

};

class EXPORT ISharedMemoryClient {
public:
    virtual ~ISharedMemoryClient() = default;

    // Connect to the shared memory.
    virtual void Connect() = 0;

    // Subscribe to a topic and return a subscription cursor.
    virtual std::unique_ptr<ISubscriptionCursor> Subscribe(const std::string& topicName) = 0;
};