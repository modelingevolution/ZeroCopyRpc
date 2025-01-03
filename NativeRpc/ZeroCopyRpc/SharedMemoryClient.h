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
#include "NamedSemaphore.h"
#include "ThreadSpin.h"
#include "ZeroCopyRpcException.h"
#include "ISharedMemoryClient.h"
using namespace boost::interprocess;
using namespace boost::uuids;



class EXPORT SharedMemoryClient : public ISharedMemoryClient
{
public:
    struct SubscriptionCursor;
private:
    static std::string ClientQueueName(const std::string& channelName);
    // forward declarations:
    
    struct Callback;
    class Topic;

    struct Callback
    {
        void* State;
        void On(void* msg) const;
        Callback(void* state, const std::function<void(void*, void*)>& func);
        Callback();

    private:
        std::function<void(void*, void*)> Func;
    };

    void InvokeUnsubscribe(const std::string& topicName, byte sloth);

    class Topic
    {
        friend struct SharedMemoryClient::SubscriptionCursor;
    public:
        TopicMetadata* Metadata = nullptr;
        SubscriptionSharedData* Subscribers = nullptr;
        CyclicBuffer* SharedBuffer = nullptr;
        SharedMemoryClient* Parent = nullptr;
        std::string Name;
        Topic(SharedMemoryClient* parent, const std::string& topicName);

        ~Topic();

        void Unsubscribe(byte sloth);
        void AckUnsubscribed(byte sloth);
        void UnsubscribeAll();

    private:
        // these are open cursors on the server, by this client.
        std::atomic<byte> _openCursorServerCount;
        std::atomic<byte> _openCursorClientCount;
        std::vector<byte> _openSlots;

        shared_memory_object* Shm = nullptr;
        mapped_region* Region = nullptr;

        std::string ShmName() const;
    };
    std::string _chName;
    message_queue _srvQueue;
    message_queue _clientQueue;
    ConcurrentDictionary<uuid, Callback> _messages;
    std::unordered_map<std::string, Topic*> _topics;
    std::thread _dispatcher;

    void DispatchResponses();

    void OnHelloReceived(void* buffer, void* promise);

    void OnSubscribed(void* buffer, void* promise);

    void OnUnsubscribed(void* buffer, void* promise);
    Topic* Get(const std::string& topic);

    Topic* GetOrCreate(const std::string& topic);

public:
    struct EXPORT SubscriptionCursor : public ISubscriptionCursor
    {
        SubscriptionCursor(byte sloth, Topic* topic);

        std::string SemaphoreName() const;

        CyclicBuffer::Accessor Read() override;
        
        bool TryReadFor(CyclicBuffer::Accessor& a, const std::chrono::milliseconds& timeout) override;
        bool TryRead(CyclicBuffer::Accessor &a) override;
        SubscriptionCursor(const SubscriptionCursor& other) = delete;

        friend void swap(SubscriptionCursor& lhs, SubscriptionCursor& rhs) noexcept;

        SubscriptionCursor(SubscriptionCursor&& other) noexcept;

        SubscriptionCursor& operator=(SubscriptionCursor other);
        SubscriptionCursor& operator=(SubscriptionCursor && other) noexcept;

        ~SubscriptionCursor() override;

    private:
        NamedSemaphore* _sem;
        byte _sloth;
        Topic* _topic;
        CyclicBuffer::Cursor* _cursor;
    };

    SharedMemoryClient(const std::string& channelName);

    void Connect() override;

    std::unique_ptr<ISubscriptionCursor> Subscribe(const std::string& topicName) override;
    ~SharedMemoryClient() override;
    
};

