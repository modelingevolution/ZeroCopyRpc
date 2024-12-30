// NativeRpc.h : Include file for standard system include files,
// or project specific include files.

#pragma once

#include <iostream>
#include <boost/interprocess/shared_memory_object.hpp>
#include <boost/interprocess/mapped_region.hpp>
#include <boost/interprocess/sync/named_semaphore.hpp>
#include <boost/interprocess/allocators/allocator.hpp>
#include <boost/interprocess/managed_shared_memory.hpp>
#include <boost/interprocess/ipc/message_queue.hpp>
#include <boost/lockfree/queue.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <atomic>
#include <semaphore>
#include <memory>
#include <string>
#include <unordered_map>
#include <memory>
#include <shared_mutex>
#include <optional>
#include <ostream>
#include <thread>
#include <future>
#include <ranges>
#ifdef WIN32
#include <windows.h>
typedef DWORD pid_t;
#else
#include <sys/types.h>
#endif
// TODO: Reference additional headers your program requires here.
#pragma once



using namespace boost::interprocess;
using namespace boost::uuids;
#include <iostream>

#include "TypeDefs.h"
#include "ConcurrentBag.hpp"
#include "ConcurrentDictionary.hpp"
#include "IDPool.hpp"
#include "CyclicMemoryPool.hpp"
#include "CyclicBuffer.hpp"
#include "Messages.h"
#include "Random.h"
#include "Export.h"

typedef CyclicBuffer<1024 * 1024 * 8, 256> CyclicBuffer8M;



// This is topic on the server side.
// When the Client subscribes, thread-safe lock-free structures need to be created, that will be used
// In publish thread - which is different that subscribe thread, this is the named-semaphore.
// When client disconnects, we only mark subscription to be disposed on the next iteration of publish loop.

struct EXPORT PublishScope
{
    PublishScope() = default;
    PublishScope(CyclicBuffer8M::WriterScope&& w, TopicService* parent);
    CyclicMemoryPool<8388608>::Span& Span();
    PublishScope(const PublishScope& other) = delete;
    ulong Type() const;

    PublishScope(PublishScope&& other) noexcept;
    ~PublishScope();

private:

    CyclicBuffer8M::WriterScope _scope;
    TopicService* _parent;
};

class EXPORT TopicService {
      

public:
    struct Subscription
    {
        named_semaphore* Sem = nullptr;
        int Index = -1;
        std::string* Name = nullptr;
        Subscription();
        Subscription(const std::string& semName, byte index = 0);
        void OpenOrCreate(const std::string& semName, byte index);

        friend bool operator==(const Subscription& lhs, const Subscription& rhs);
        friend bool operator!=(const Subscription& lhs, const Subscription& rhs);
        void Close();

    };
    friend struct PublishScope;
    inline static std::string ShmName(const std::string& channel_name, const std::string& topic_name);
        

    void RemoveDanglingSubscriptionEntry(int i, SubscriptionSharedData& sub) const;
    static bool ClearIfExists(const std::string& channel_name, const std::string& topic_name);
    TopicService(const std::string& channel_name, const std::string& topic_name);

    inline std::string GetSubscriptionSemaphoreName(pid_t pid, int index) const;

    template<typename T, typename... Args>
    void Publish(ulong type, Args&&... args) {
        auto scope = Prepare(sizeof(T), type);
        auto& span = scope.Span();
        auto ptr = new (span.Start) T(std::forward<Args>(args)...);
        span.Commit(sizeof(T));
    }


    PublishScope Prepare(ulong minSize, ulong type);
    byte Subscribe(pid_t pid);
    bool Unsubscribe(pid_t pid, byte id) const;
    std::string Name();
    ~TopicService();
private:
    std::string _channelName;
    std::string _topicName;

    // Client Semaphore table
    ConcurrentBag<Subscription, 256> _subscriptions;
    IDPool256 _idPool;

    shared_memory_object* _shm;
    mapped_region* _region;

    // IN SHM
    // Client PID, Notified, Current Offset table.
    SubscriptionSharedData* _subscribers; // 256

    // IN SHM
    CyclicBuffer8M* _buffer;

    void NotifyAll();
};


class EXPORT SharedMemoryServer {
private:
    std::string _chName;

    std::unordered_map<std::string, TopicService*> _topics;
    std::unordered_map<pid_t, message_queue*> _clients;
    message_queue _messageQueue;
    std::thread dispatcher;

    byte Subscribe(const char* topicName, pid_t pid);
    bool OnUnsubscribe(const char* topicName, pid_t pid, byte id);

    message_queue* GetClient(pid_t pid);

    void OnHelloResponse(pid_t pid, std::chrono::time_point<std::chrono::steady_clock> now, const uuid &correlationId);

    void DispatchMessages();
    
    TopicService* CreateSubscription(const char *topicName);
    bool RemoveSubscription(const char* topicName);
public:
    SharedMemoryServer(const std::string& channel);

    ~SharedMemoryServer();

    
    TopicService* CreateTopic(const std::string& topicName);
    bool RemoveTopic(const std::string& topicName);
};