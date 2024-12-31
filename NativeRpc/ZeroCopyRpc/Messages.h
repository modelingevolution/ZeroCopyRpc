#pragma once

#include "TypeDefs.h"
#include <boost/uuid/uuid.hpp>
#include "Random.h"
#include "ProcessUtils.h"

using namespace boost::uuids;

// forward declaration
class TopicService;

#pragma pack(push, 1)
struct RemoveSubscription
{
    char TopicName[256];
    inline void SetTopicName(const std::string& str)
    {
        strncpy_s(TopicName, str.c_str(), sizeof(TopicName) - 1);  // Leave room for null terminator
    }
    friend std::ostream& operator<<(std::ostream& os, const RemoveSubscription& obj)
    {
        return os << "Topic name: " << obj.TopicName;
    }
};
struct CreateSubscription
{
    char TopicName[256];
    inline void SetTopicName(const std::string& str)
    {
        strncpy_s(TopicName, str.c_str(), sizeof(TopicName) - 1);  // Leave room for null terminator
    }
    friend std::ostream& operator<<(std::ostream& os, const CreateSubscription& obj)
    {
        return os << "Topic name: " << obj.TopicName;
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
        Pid = GetCurrentProcessId();
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
        Pid = GetCurrentProcessId();
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
    char TopicName[256];
    inline void SetTopicName(const std::string& str)
    {
        strncpy_s(TopicName, str.c_str(), sizeof(TopicName) - 1);  // Leave room for null terminator
    }
};
struct SubscribeCommand {

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
        return os << "Topic name: " << obj.TopicName << std::endl;
    }
private:

};

struct TopicMetadata
{
    ulong BufferSize;
    ulong SubscribesTableSize;
    ulong TotalSize()
    {
        return sizeof(TopicMetadata) + BufferSize + SubscribesTableSize;
    }
    void* MetadataAddress(void* base) { return base; }
    void* SubscribersTableAddress(void* base) { return (void*)((size_t)base + sizeof(TopicMetadata)); }
    void* BuffserAddress(void* base) { return (void*)((size_t)base + sizeof(TopicMetadata) + SubscribesTableSize); }
};
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
#pragma pack(pop)

typedef RequestEnvelope<SubscribeCommand, 1> SubscribeCommandEnvelope;
typedef ResponseEnvelope<SubscribeResponse, 5> SubscribeResponseEnvelope;

typedef RequestEnvelope<UnSubscribeCommand, 6> UnSubscribeCommandEnvelope;
typedef ResponseEnvelope<UnSubscribeResponse, 7> UnSubscribeResponseEnvelope;

typedef RequestResponseEnvelope<CreateSubscription, 2, TopicService*> CreateSubscriptionEnvelope;
typedef RequestResponseEnvelope<RemoveSubscription, 8, bool> RemoveSubscriptionEnvelope;

typedef RequestEnvelope<HelloCommand, 3> HelloCommandEnvelope;
typedef ResponseEnvelope<HelloResponse, 4> HelloResponseEnvelope;