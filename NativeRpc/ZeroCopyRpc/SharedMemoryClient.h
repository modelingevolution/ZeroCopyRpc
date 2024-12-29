#pragma once
#include <functional>
#include <future>
#include <iostream>

#include <boost/interprocess/shared_memory_object.hpp>
#include <boost/interprocess/mapped_region.hpp>
#include <boost/interprocess/sync/named_semaphore.hpp>
#include <boost/interprocess/managed_shared_memory.hpp>
#include <boost/interprocess/ipc/message_queue.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/lockfree/queue.hpp>
#include <shared_mutex>
#include <ostream>
#include <thread>
#include <future>
#include <ranges>
#include <semaphore>
#include <memory>
#include <unordered_map>

#include "TypeDefs.h"
#include "ConcurrentBag.hpp"
#include "ConcurrentDictionary.hpp"
#include "IDPool.hpp"
#include "CyclicMemoryPool.hpp"
#include "CyclicBuffer.hpp"
#include "Messages.h"
#include "Random.h"
#include "ProcessUtils.h"
#include "Export.h"
using namespace boost::interprocess;
using namespace boost::uuids;

class EXPORT SharedMemoryClient
{
private:
    struct Callback
    {
        void* State;
        void On(void* msg) const;
        Callback(void* state, const std::function<void(void*, void*)>& func);
        Callback();

    private:
        std::function<void(void*, void*)> Func;
    };

    void Unsubscribe(const std::string& topicName, byte sloth);

    class Topic
    {

    public:
        TopicMetadata* Metadata = nullptr;
        SubscriptionSharedData* Subscribers = nullptr;
        CyclicBuffer<1024 * 1024 * 8, 256>* SharedBuffer = nullptr;
        SharedMemoryClient* Parent = nullptr;
        std::string Name;
        Topic(SharedMemoryClient* parent, const std::string& topicName);

        ~Topic();

        void Unsubscribe(byte sloth);

    private:

        shared_memory_object* Shm = nullptr;
        mapped_region* Region = nullptr;

        std::string ShmName() const;
    };
    std::string _chName;
    message_queue _srvQueue;
    message_queue _clientQueue;
    ConcurrentDictionary<uuid, Callback> _messages;
    std::unordered_map<std::string, Topic*> _subscriptions;

    void DispatchResponses();

    void OnHelloReceived(void* buffer, void* promise);

    void OnSubscribed(void* buffer, void* promise);

    void OnUnsubscribed(void* buffer, void* promise);

    Topic* GetOrCreate(const std::string& topic);

public:
    struct SubscriptionCursor
    {
        SubscriptionCursor(byte sloth, Topic* topic);

        std::string SemaphoreName() const;

        CyclicBuffer<1024 * 1024 * 8, 256>::Accessor Read();
        SubscriptionCursor(const SubscriptionCursor& other) = delete;

        friend void swap(SubscriptionCursor& lhs, SubscriptionCursor& rhs) noexcept;

        SubscriptionCursor(SubscriptionCursor&& other) noexcept;

        SubscriptionCursor& operator=(SubscriptionCursor other);
        SubscriptionCursor& operator=(SubscriptionCursor && other) noexcept;

        ~SubscriptionCursor();

    private:
        named_semaphore* _sem;
        byte _sloth;
        Topic* _topic;
        CyclicBuffer<1024 * 1024 * 8, 256>::Cursor* _cursor;
    };

    SharedMemoryClient(const std::string& channelName);

    void Connect();

    std::unique_ptr<SubscriptionCursor> Subscribe(const std::string& topicName);
};
