#pragma once

#include "TypeDefs.h"
#include <boost/uuid/uuid.hpp>
#include "Random.h"
#include "ProcessUtils.h"
#include "CrossPlatform.h"

using namespace boost::uuids;

// forward declaration
class TopicService;

#pragma pack(push, 1)
struct RemoveTopic
{
    char TopicName[256];
    inline void SetTopicName(const std::string& str)
    {
        strncpy_s(TopicName, str.c_str(), sizeof(TopicName) - 1);  // Leave room for null terminator
    }
    friend std::ostream& operator<<(std::ostream& os, const RemoveTopic& obj)
    {
        return os << "Topic name: " << obj.TopicName;
    }
};
struct CreateTopic
{
    char TopicName[256];
    unsigned int MaxMessageCount;
    unsigned int BufferSize;

    inline void SetTopicName(const std::string& str)
    {
        strncpy_s(TopicName, str.c_str(), sizeof(TopicName) - 1);  // Leave room for null terminator
    }
    friend std::ostream& operator<<(std::ostream& os, const CreateTopic& obj)
    {
        return os << "Name: " << obj.TopicName << " Message Capacity: " << obj.MaxMessageCount << ", Buffer Size: " << obj.BufferSize << "B )";
    }
};

template <typename T, ulong TTypeId>
struct RequestEnvelope
{
    ulong Type = TTypeId;
    uuid CorrelationId;
    pid_t Pid;
    T Request;
    RequestEnvelope()
    {
        //TODO
        CorrelationId = Random::Shared();
        Pid = getCurrentProcessId();
    }
private:

};
template <typename T, ulong TTypeId>
struct ResponseEnvelope
{
    ulong Type = TTypeId;
    uuid CorrelationId;
    T Response;

private:

};
template <typename T, ulong TTypeId, typename TResponse>
struct RequestResponseEnvelope
{
    ulong Type = TTypeId;
    uuid CorrelationId;
    pid_t Pid;
    T Request;

    RequestResponseEnvelope() : Request()
    {
        Promise = new std::promise<TResponse>();
        //TODO
        CorrelationId = Random::Shared();
        Pid = getCurrentProcessId();
    }
    ~RequestResponseEnvelope()
    {
        delete Promise;
        Promise = nullptr;
    }
    inline TResponse Response()
    {
        return Promise->get_future().get();
    }

    inline void Set(TResponse value)
    {
        Promise->set_value(value);
    }

private:
    std::promise<TResponse>* Promise;

};
struct HelloCommand
{
    std::chrono::time_point<std::chrono::high_resolution_clock> Created = std::chrono::high_resolution_clock::now();
};
struct HelloResponse
{
    std::chrono::time_point<std::chrono::high_resolution_clock> RequestCreated;

};
struct SubscribeResponse
{
    // SubscribersTableIndex
    byte Id;
};
struct UnSubscribeCommand
{
    // SubscribersTableIndex
    byte SlothId;
    // TODO: Should be char*, and have static SizeOf method. Allocation should be done by in-place operator.
    char TopicName[256];
    inline void SetTopicName(const std::string& str)
    {
        strncpy_s(TopicName, str.c_str(), sizeof(TopicName) - 1);  // Leave room for null terminator
    }
};
struct UnSubscribeResponse
{
    bool IsSuccess;
    byte SlothId;
    // TODO: Should be char*, and have static SizeOf method. Allocation should be done by in-place operator.
    char TopicName[256];
    inline void SetTopicName(const std::string& str)
    {
        strncpy_s(TopicName, str.c_str(), sizeof(TopicName) - 1);  // Leave room for null terminator
    }
};

struct SubscribeCommand {
	// TODO: Should be char*, and have static SizeOf method. Allocation should be done by in-place operator.
    char TopicName[256];

    SubscribeCommand() : TopicName{}
    {

    }

    inline void SetTopicName(const std::string& str)
    {
        strncpy_s(TopicName, str.c_str(), sizeof(TopicName) - 1);  // Leave room for null terminator
    }
    friend std::ostream& operator<<(std::ostream& os, const SubscribeCommand& obj)
    {
        return os << "Topic name: " << obj.TopicName;
    }
private:

};

struct TopicMetadata
{
    ulong TotalBufferSize;
    ulong SubscribesTableSize;
    ulong BufferItemCapacity;
    ulong BufferSize;

    ulong TotalSize()
    {
        return sizeof(TopicMetadata) + TotalBufferSize + SubscribesTableSize;
    }
    void* MetadataAddress(void* base) { return base; }
    void* SubscribersTableAddress(void* base) { return (void*)((size_t)base + sizeof(TopicMetadata)); }
    void* BufferAddress(void* base) { return (void*)((size_t)base + sizeof(TopicMetadata) + SubscribesTableSize); }
};

#pragma pack(pop)

struct SubscriptionSharedData
{
    ulong NextIndex;
    std::atomic<ulong> Notified;
    std::atomic<bool> PendingRemove;
    std::atomic<bool> Active;
    pid_t Pid;
    void Reset(pid_t pid) {
        Pid = pid;
        Notified.store(0);
        Active.store(true);
        PendingRemove.store(false);
    }
};

typedef RequestEnvelope<SubscribeCommand, 1> SubscribeCommandEnvelope;
typedef ResponseEnvelope<SubscribeResponse, 5> SubscribeResponseEnvelope;

typedef RequestEnvelope<UnSubscribeCommand, 6> UnSubscribeCommandEnvelope;
typedef ResponseEnvelope<UnSubscribeResponse, 7> UnSubscribeResponseEnvelope;

typedef RequestResponseEnvelope<CreateTopic, 2, TopicService*> CreateSubscriptionEnvelope;
typedef RequestResponseEnvelope<RemoveTopic, 8, bool> RemoveSubscriptionEnvelope;

typedef RequestEnvelope<HelloCommand, 3> HelloCommandEnvelope;
typedef ResponseEnvelope<HelloResponse, 4> HelloResponseEnvelope;