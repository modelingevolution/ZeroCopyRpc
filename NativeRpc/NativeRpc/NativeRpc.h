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
#ifdef WIN32
#include <windows.h>
pid_t getCurrentProcessId() {
    return GetCurrentProcessId();
}
#else
#include <unistd.h>
pid_t getCurrentProcessId() {
    return getpid();
}
#endif


struct EventSlot {
    unsigned int Type;
    unsigned long Offset;
    unsigned long Size;
};

struct SubscriptionCursor {
    std::atomic<unsigned long> CurrentIndex;
    std::atomic<unsigned long> ReadPosition;
};

typedef unsigned char byte;
typedef unsigned long ulong;
typedef unsigned int uint;

template <typename Key, typename Value>
class ConcurrentDictionary {
private:
    std::unordered_map<Key, Value> map;
    mutable std::shared_mutex mtx;
public:
    // Inserts or updates a key-value pair
    void InsertOrUpdate(const Key& key, const Value& value) {
        std::unique_lock<std::shared_mutex> lock(mtx);
        map[key] = value;
    }
    // Tries to get the value associated with a key
    std::optional<Value> TryGetValue(const Key& key) const {
        std::shared_lock<std::shared_mutex> lock(mtx);
        auto it = map.find(key);
        if (it != map.end()) {
            return it->second;
        }
        return std::nullopt;
    }
    // Removes a key-value pair
    bool Remove(const Key& key) {
        std::unique_lock<std::shared_mutex> lock(mtx);
        return map.erase(key) > 0;
    }
    // Checks if a key exists
    bool ContainsKey(const Key& key) const {
        std::shared_lock<std::shared_mutex> lock(mtx);
        return map.find(key) != map.end();
    }
    // Clears the dictionary
    void Clear() {
        std::unique_lock<std::shared_mutex> lock(mtx);
        map.clear();
    }
};

template<unsigned long TSIZE>
class CyclicMemoryPool
{
private:
    byte _buffer[TSIZE];
    size_t _offset;
    std::atomic<bool> _inUse;
    
    size_t remaining() const { return TSIZE - _offset; }

public:
    
    struct Span {
        byte* Start;
        size_t Size;

        Span(byte* ptr, size_t size, CyclicMemoryPool* parent)
            : Start(ptr), Size(size), _parent(parent), _committed(0) {
        }

        // Disallow copying
        Span(const Span&) = delete;

        // Allow move semantics
        Span(Span&& other) noexcept
            : Start(other.Start), Size(other.Size), _parent(other._parent), _committed(other._committed) {
            other.Start = nullptr;
            other.Size = 0;
            other._parent = nullptr;
        }
        size_t StartOffset() { return Start - _parent->_buffer; }
        size_t EndOffset() { return Start - _parent->_buffer + _committed; }
        size_t CommitedSize() const { return _committed; }
        byte* End() const { return Start + _committed; }
        void Commit(size_t size) {
            if (size > (Size - _committed)) {
                throw std::runtime_error("Commit size exceeds reserved span.");
            }
            _committed += size;
            _parent->_offset += size;  // Move parent pointer forward
        }

        ~Span() {
            if (_parent) {
                _parent->_inUse.store(false);  // Release the lock
            }
        }

    private:
        CyclicMemoryPool* _parent;
        size_t _committed;
    };

    CyclicMemoryPool()
        : _offset(0), _inUse(false), _buffer(0) {
    }
    byte* Get(size_t offset)
    {
        return _buffer + offset;
    }
    size_t Size() const { return TSIZE; }
    byte* End() { return _buffer + _offset; }
    Span GetWriteSpan(size_t minSize) {
        if (minSize > TSIZE) {
            throw std::runtime_error("Requested size exceeds buffer capacity.");
        }

        size_t freeSpace = remaining();
        bool expected = false;

        // Try to acquire the lock
        if (!_inUse.compare_exchange_strong(expected, true)) {
            throw std::runtime_error("Buffer is already in use.");
        }

        // Check if there is enough space, or reset the pointer to reuse the buffer
        if (freeSpace < minSize) {
            _offset = 0;
            freeSpace = TSIZE;
        }

        return Span(this->End(), freeSpace, this);
    }
};

template<unsigned long TSIZE, unsigned long TCAPACITY>
class CyclicBuffer
{
public:
    struct Item
    {
        size_t Size;
        ulong Type;
        size_t Offset;
    };
    struct Accessor
    {
        Item* Item;
        CyclicBuffer* Buffer;
        template<typename T>
    	T* As () const
        {
            return (T*)(Buffer->_memory.Get(Item->Offset));
        }
        Accessor(const Accessor&) = delete;
        Accessor(Accessor&& other) noexcept : Item(other->Item), Buffer(other->Buffer) {
            other.Item = nullptr;
            other.Buffer = nullptr;
        }

        Accessor(::CyclicBuffer<TSIZE, TCAPACITY>::Item* item, CyclicBuffer<TSIZE, TCAPACITY>* buffer)
	        : Item(item),
	          Buffer(buffer)
        {
        }

        inline byte* Get() { return Buffer->_memory.Get(Item->Offset); }
    };
    struct WriterScope
    {
        CyclicMemoryPool<TSIZE>::Span Span;
        unsigned long Type;
        ~WriterScope()
        {
	        auto written = Span.CommitedSize();
	        if(written > 0)
	        {
                _parent->_items[_parent->_nextIndex++ % TCAPACITY] = Item{ written, Type, Span.StartOffset() };
	        }
        }
        WriterScope(const WriterScope&) = delete;
        WriterScope(WriterScope&& other) noexcept
            : Span(std::move(other.Span)),
            Type(other.Type),
            _parent(other._parent)
        {
            other._parent = nullptr;
        }
        WriterScope(CyclicMemoryPool<TSIZE>::Span&& span, unsigned long type, CyclicBuffer<TSIZE, TCAPACITY>* parent)
            : Span(std::move(span)), // Move the span
            _parent(parent),
            Type(type)
        {
        }

    private:
        CyclicBuffer* _parent;

    };
    struct Cursor
    {
        unsigned long Index;
        unsigned long Type() { }
        unsigned long Remaining() const { return _parent->_nextIndex - Index; }
        Accessor Data() const
        {
	        auto item = &(_parent->_items[(Index-1) % TCAPACITY]);
            return Accessor(item, _parent);
        }
        Cursor(unsigned long index, CyclicBuffer<TSIZE, TCAPACITY>* parent)
            : Index(index),
            _parent(parent)
        {
        }
        // Disallow copying
        Cursor(const Cursor&) = delete;
        bool TryRead()
        {
            auto diff = _parent->_nextIndex - Index;
            if(diff > 0)
            {
                Index += 1;
                return true;
            }
            return false;
        }
        // Allow move semantics
        Cursor(Cursor&& other) noexcept:
    	Index(other.Index), _parent(other._parent)
    	{
            other.Index = 0ul - 1;
        	other._parent = nullptr;
        }
    private:
        CyclicBuffer* _parent;


    };
    Cursor ReadNext()
    {
        return ReadNext(_nextIndex);
    }
    Cursor ReadNext(ulong nextValue)
    {
        return Cursor(nextValue, this);
    }
    WriterScope WriteScope(ulong minSize, ulong type)
    {
        auto span = _memory.GetWriteSpan(minSize);
        return WriterScope(std::move(span), type, this); // Explicitly use std::move for the span
    }
    ulong NextIndex() const
    {
        return _nextIndex;
    }
private:
    ulong _nextIndex = 0;

    // We constantly check if there is enough memory in the CyclicMemoryPool for all the Items in the ring.
    std::atomic<ulong> _messageQueueItemsSize;

    CyclicMemoryPool<TSIZE> _memory;
    Item _items[TCAPACITY];

};
struct Random
{
    static random_generator Shared;
};

#pragma pack(push, 1) 
template <typename T, ulong TTypeId>
struct RequestEnvelope
{
    ulong Type = TTypeId;
    uuid CorrelationId;
    pid_t Pid;
    T Request;
    RequestEnvelope()
    {
	    CorrelationId = Random::Shared();
        Pid = GetCurrentProcessId();
    }
private :
    
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
	std::chrono::time_point<std::chrono::steady_clock> Now = std::chrono::high_resolution_clock::now();
};
struct HelloResponse
{
    std::chrono::time_point<std::chrono::steady_clock> From;
};
struct SubscribeResponse
{
    byte SubscribersTableIndex;
};
struct SubscribeCommand {

    char TopicName[256];
    
    SubscribeCommand() :  TopicName{}
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
    ulong StartOffset;
    std::atomic<ulong> Notified;
    pid_t Pid;
};
#pragma pack(pop) 



class Topic {
public:
    struct Subscription
    {
        named_semaphore* Sem;
        
        Subscription(const std::string& semName) : Sem(nullptr)
        {
            Sem = new named_semaphore(create_only, semName.c_str(),0);
            std::cout << "named semaphore created: " << semName << std::endl;
        }

        Subscription(const Subscription& other) = delete;
	       

        Subscription(Subscription&& other) noexcept
	        : Sem(other.Sem)
        {
            other.Sem = nullptr;
        }


        Subscription& operator=(Subscription&& other) noexcept
        {
	        if (this == &other)
		        return *this;
	        Sem = other.Sem;
            other.Sem = nullptr;

	        return *this;
        }

        ~Subscription()
        {
	        delete Sem;
        }

        Subscription() : Sem(nullptr) {}
    };
private:
    
    std::string _channelName;
    std::string _topicName;

	Subscription _subscriptions[256];
    SubscriptionSharedData* _subscribers; // 256

    shared_memory_object _shm;
    mapped_region* _region;
    std::atomic<byte> _count;

    CyclicBuffer<1024*1024*8,256>* _buffer;
    
    void NotifyAll()
    {
        auto count = _count.load();
        for(byte i = 0; i < count; i++)
        {
            auto& s = _subscriptions[i];
            auto& data = _subscribers[i];
            if(data.Notified.fetch_add(1) == 0)
            {
	            // this is the first time, need to set the cursors index.
                data.StartOffset = _buffer->NextIndex() - 1;
            }
            
        	s.Sem->post();
            
        }
    }
public:
    
    static std::string ShmName(const std::string& channel_name, const std::string& topic_name)
    {
        return channel_name + "." + topic_name + ".buffer";
    }
    struct PublishScope
    {
	    PublishScope() = default;
	    PublishScope(CyclicBuffer<1024 * 1024 * 8, 256>::WriterScope&& w, Topic* parent) : _scope( std::move(w)), _parent(parent)
	    {
		    
	    }
        auto& Span() { return _scope.Span; }
        PublishScope(const PublishScope& other) = delete;
        ulong Type() const { return _scope.Type; }

	    PublishScope(PublishScope&& other) noexcept
		    : _scope(std::move(other._scope))
	    {
            other._parent = nullptr;
	    }


        ~PublishScope()
        {
	        if(_parent != nullptr && _scope.Span.CommitedSize() > 0)
	        {
                _parent->NotifyAll();
                _parent = nullptr;
	        }
        }
    private:

        CyclicBuffer<1024 * 1024 * 8, 256>::WriterScope _scope;
        Topic *_parent;
    };
    Topic(const std::string& channel_name, const std::string& topic_name)
	    : _channelName(channel_name),
	      _topicName(topic_name),
		_subscriptions(),
		_count(0),
    _shm(create_only, ShmName(channel_name,topic_name).c_str(), read_write)
    {
        TopicMetadata m = {
        	sizeof(CyclicBuffer<1024 * 1024 * 8, 256>),
         sizeof(SubscriptionSharedData) * 256 };

        _shm.truncate(m.TotalSize());

        _region = new mapped_region(_shm, read_write);
        auto dst = _region->get_address();
        memset(dst, 0,m.TotalSize());

        TopicMetadata* metadata = (TopicMetadata*)dst;
        *metadata = m; // copy
    	
        _subscribers = (SubscriptionSharedData*)m.SubscribersTableAddress(dst);
        _buffer = (CyclicBuffer<1024 * 1024 * 8, 256>*)m.BuffserAddress(dst);
    }

    byte Subscribe(pid_t pid)
    {
        auto index = _count.fetch_add(1);
        std::ostringstream oss;
        oss << _channelName << "." << _topicName << "." << pid << "." << (int)index << ".sem";
        std::string semName = oss.str();
    	
        if (index >= 256) {
            _count.fetch_sub(1);
            throw std::out_of_range("Subscription index out of bounds");
        }
        _subscriptions[index] = Subscription(semName);
        return index;
    }
    PublishScope Prepare(ulong minSize, ulong type)
    {
        return PublishScope(_buffer->WriteScope(minSize,type), this);
    }
    

    ~Topic() {
        delete _region;
        _shm.remove(_shm.get_name());
    }
};
#pragma pack(push, 1) 
struct CreateSubscription
{
    char TopicName[256];
    inline void SetTopicName(const std::string &str)
    {
        strncpy_s(TopicName, str.c_str(), sizeof(TopicName) - 1);  // Leave room for null terminator
    }
    friend std::ostream& operator<<(std::ostream& os, const CreateSubscription& obj)
    {
        return os << "Topic name: " << obj.TopicName;
    }
};
#pragma pack(pop)

typedef RequestEnvelope<SubscribeCommand,1> SubscribeCommandEnvelope;
typedef RequestResponseEnvelope<CreateSubscription, 2,Topic*> CreateSubscriptionEnvelope;
typedef RequestEnvelope<HelloCommand, 3> HelloCommandEnvelope;
typedef ResponseEnvelope<HelloResponse, 4> HelloResponseEnvelope;
typedef ResponseEnvelope<SubscribeResponse, 5> SubscribeResponseEnvelope;

class SharedMemoryClient
{
private:
    struct Callback
    {
        void* State;
        void On(void *msg)
        {
            Func(msg, State);
        }

        Callback(void* state, const std::function<void(void*, void*)>& func)
	        : State(state),
	          Func(func)
        {
        }
        Callback() : State(nullptr), Func(nullptr)
        {
	        
        }
        

    private:
        std::function<void(void*, void*)> Func;
    };
    class Topic
    {
   
        public:
            TopicMetadata* Metadata = nullptr;
            SubscriptionSharedData* Subscribers = nullptr;
            CyclicBuffer<1024 * 1024 * 8, 256>* SharedBuffer = nullptr;
            SharedMemoryClient* Parent = nullptr;
            std::string Name;
    		Topic(SharedMemoryClient *parent, const std::string &topicName) : Name(topicName), Parent(parent)
	        {
	            Shm = new shared_memory_object(open_only, ShmName().c_str(), read_only);
	            Region = new mapped_region(*Shm, read_only);
	            auto base = Region->get_address();
	            Metadata = (TopicMetadata * )base;
	            Subscribers = (SubscriptionSharedData*)Metadata->SubscribersTableAddress(base);
	            SharedBuffer = (CyclicBuffer<1024 * 1024 * 8, 256>*)Metadata->BuffserAddress(base);
	        }
	        ~Topic()
    		{
	            SharedBuffer = nullptr;
	            Subscribers = nullptr;
	            Metadata = nullptr;
	            delete Region;
	            Shm->remove(Shm->get_name());
	            delete Shm;
    		}
	    private:
	        
	        shared_memory_object* Shm = nullptr;
	        mapped_region* Region = nullptr;

	        std::string ShmName()
	        {
	            std::ostringstream oss;
	            oss << Parent->_chName << "." << Name << ".buffer";
	            return oss.str();
	        }
    
    };
	std::string _chName;
    message_queue _srvQueue;
    message_queue _clientQueue;
    ConcurrentDictionary<uuid, Callback> _messages;
    std::unordered_map<std::string, Topic*> _subscriptions;

    void DispatchResponses()
    {
        byte buffer[1024];
        size_t recSize;
        uint priority;
        uuid& correlationId = *((uuid*)(buffer+sizeof(ulong)));
        while (true)
        {
            auto timeout = std::chrono::time_point<std::chrono::high_resolution_clock>::clock::now();
            timeout = timeout + std::chrono::seconds(30);
            if (_clientQueue.timed_receive(buffer, 1024, recSize, priority, timeout))
            {
                // auto callbackDelegate
                
                auto msg = _messages.TryGetValue(correlationId);
                if (msg.has_value())
                    msg.value().On(buffer);
            }
            else
                std::cout << "No message has been received." << std::endl;
        }
    }
    void OnHelloReceived(void *buffer, void *promise)
    {
	    auto ptr = (HelloResponseEnvelope*)buffer;
        auto p = (std::promise<HelloResponseEnvelope*>*)promise;
        p->set_value(new HelloResponseEnvelope(*ptr)); // default copy-ctor;
        _messages.Remove(ptr->CorrelationId);
    }
    void OnSubscribed(void* buffer, void* promise)
    {
        auto ptr = (SubscribeResponseEnvelope*)buffer;
        auto p = (std::promise<SubscribeResponseEnvelope*>*)promise;
        p->set_value(new SubscribeResponseEnvelope(*ptr)); // default copy-ctor;
        _messages.Remove(ptr->CorrelationId);
    }
    Topic* GetOrCreate(const std::string &topic)
    {
        auto it = _subscriptions.find(topic);
        Topic* t = nullptr;
        if(it == _subscriptions.end())
        {
	        // we need to create new.
            t = new Topic(this, topic);
            _subscriptions.emplace(topic, t);
        }
    	else
        {
            t = it->second;
        }
        return t;
    }
public:
    struct SubscriptionCursor
    {
        SubscriptionCursor(byte sloth, Topic* topic) : _sem(nullptr), _sloth(sloth), _topic(topic), _cursor(nullptr)
        {
            _sem = new named_semaphore(open_only, SemaphoreName().c_str());
            auto value = _topic->Subscribers[sloth].StartOffset;
            _cursor = new CyclicBuffer<1024 * 1024 * 8, 256>::Cursor(_topic->SharedBuffer->ReadNext(value)); // move ctor.
        }
        std::string SemaphoreName() const
        {
            std::ostringstream oss;
            oss << this->_topic->Parent->_chName << "." << _topic->Name << "." << (ulong)GetCurrentProcessId() << "." << (int)_sloth << ".sem";
            return oss.str();
        }
        CyclicBuffer<1024 * 1024 * 8, 256>::Accessor Read()
        {
            _sem->wait();
            if (_cursor->TryRead())
                return _cursor->Data();
            else throw std::exception("TryRead returned false.");
        }

    private:
        named_semaphore* _sem;
        byte _sloth;
        Topic* _topic;
        CyclicBuffer<1024 * 1024 * 8, 256>::Cursor* _cursor;
    };

    SharedMemoryClient(const std::string& channelName) :
		_chName(channelName),
		_srvQueue(open_only, channelName.c_str()),
		_clientQueue(create_only, (channelName +"."+ std::to_string(getCurrentProcessId())).c_str(), 256, 1024)
    {
        std::thread([this]() { DispatchResponses(); }).detach();
    }
    void Connect()
    {
        HelloCommandEnvelope env;
        auto delegate = std::bind(&SharedMemoryClient::OnHelloReceived, this, std::placeholders::_1, std::placeholders::_2);
        std::promise<HelloResponseEnvelope*> promise;
        Callback c(&promise, delegate);
        _messages.InsertOrUpdate(env.CorrelationId, c);
        _srvQueue.send(&env, sizeof(HelloCommandEnvelope),0);
        auto value = promise.get_future().get();
        delete value;
        auto duration = std::chrono::high_resolution_clock::now() - value->Response.From;
        std::cout << "Connected. It took: " << duration << std::endl;

    }

    std::unique_ptr<SubscriptionCursor> Subscribe(const std::string& topicName)
    {
	    SubscribeCommandEnvelope env;
        auto delegate = std::bind(&SharedMemoryClient::OnSubscribed, this, std::placeholders::_1, std::placeholders::_2);
        env.Request.SetTopicName(topicName);

        std::promise<SubscribeResponseEnvelope*> promise;
        Callback c(&promise, delegate);
        _messages.InsertOrUpdate(env.CorrelationId, c);

        std::cout << "Sending message size: " << sizeof(SubscribeCommandEnvelope) << std::endl;
        _srvQueue.send(&env, sizeof(SubscribeCommandEnvelope), 0);
        
        auto value = promise.get_future().get();

        auto topic = GetOrCreate(topicName);
        auto result = std::make_unique<SubscriptionCursor>(value->Response.SubscribersTableIndex, topic);

        delete value;
        return result;
    }
};

class SharedMemoryServer {
private:
    std::string _chName;

    std::unordered_map<std::string, Topic*> _topics;
    std::unordered_map<pid_t, message_queue*> _clients;
    message_queue _messageQueue;
   
    byte Subscribe(const char* topicName, pid_t pid)
    {
	    // construct std::string out of str,
        // find the topic in _topics
        // delegate Subscribe to Topic
        std::string key(topicName);
        auto it = _topics.find(key);
        byte sloth = 0;
        if(it != _topics.end())
        {
	        // we have found
            sloth = it->second->Subscribe(pid);
            return sloth;
        }
        else
        {
	        // we should communicate back using client's message queue (not yet implemented).
            // we should create a topic.
            
        }
        return 0;
    }

    message_queue* GetClient(pid_t pid)
    {
        auto it = _clients.find(pid);
        message_queue* m;
        if (it == _clients.end())
        {
            // we need to connect.
            std::string rspMsgQueue = _chName + "." + std::to_string(pid);
            m = new message_queue(open_only, rspMsgQueue.c_str());
            _clients.emplace(pid, m);
        }
        else m = it->second;
        return m;
    }
    void OnHelloResponse(pid_t pid, std::chrono::time_point<std::chrono::steady_clock> now, const uuid &correlationId)
    {
        message_queue* m = GetClient(pid);
        HelloResponseEnvelope env;
        env.CorrelationId = correlationId;
        env.Response.From = now;
        m->send(&env, sizeof(HelloResponseEnvelope), 0);

    }
    

    void DispatchMessages()
    {
        byte buffer[1024];
        size_t recSize;
        uint priority;
        ulong& messageType = *((ulong*)buffer);
	    while(true)
	    {
            auto timeout = std::chrono::time_point<std::chrono::high_resolution_clock>::clock::now();
            timeout = timeout + std::chrono::seconds(30);
            if (_messageQueue.timed_receive(buffer, 1024, recSize, priority, timeout))
            {
                switch(messageType)
                {
                case 1:
                {
                    auto& env = *(SubscribeCommandEnvelope*)buffer;
                    std::cout << "Subscribe to topic, " << env.Request << std::endl;
                    SubscribeResponseEnvelope rsp;
                    rsp.CorrelationId = env.CorrelationId;
                    rsp.Response.SubscribersTableIndex = this->Subscribe(env.Request.TopicName, env.Pid);
                    if(!GetClient(env.Pid)->try_send(&rsp, sizeof(SubscribeResponseEnvelope), 0))
                    {
                        std::cout << "Cannot send message to client." << std::endl;
                    }

                    break;
                }
                case 2:
                {
                    auto& env= *(CreateSubscriptionEnvelope*)buffer;
                    std::cout << "Create subscription, " << env.Request << std::endl;
                    env.Set(this->OnCreateSubscription(env.Request.TopicName));
                    break;
                }
                case 3:
	            {
                	auto& env = *(HelloCommandEnvelope*)buffer;
                	std::cout << "Hello request from Pid: " << env.Pid << std::endl;
                    this->OnHelloResponse(env.Pid, env.Request.Now, env.CorrelationId);
                    break;
	            }
                default:
                    break;
                }
            }
            else
                std::cout << "No message has been received." << std::endl;
	    }
    }
    Topic* OnCreateSubscription(const char *topicName)
    {
       std::string key(topicName);
       auto it = _topics.find(key);
       if (it != _topics.end())
       {
           // we have found
           auto topic = it->second;
           return topic;
       }
       else
       {
           shared_memory_object::remove(Topic::ShmName(this->_chName, topicName).c_str());
           auto result = new Topic(this->_chName, topicName);
           
           _topics.emplace(topicName, result);
           return result;
       }
       //delete promise;
    }
public:
    SharedMemoryServer(const std::string& channel) : _chName(channel), _messageQueue(open_or_create, channel.c_str(), 128, 512)
    {
        std::thread([this]() { DispatchMessages(); }).detach();
    }
    ~SharedMemoryServer()
    {
	    for (const auto& t : _topics | std::views::values)
            delete t;
	    
    }
    Topic* CreateTopic(const std::string& topicName)
	{
        CreateSubscriptionEnvelope env;
        env.Request.SetTopicName(topicName);

        _messageQueue.send(&env, sizeof(CreateSubscriptionEnvelope), 0);
        
        return env.Response();
    }


};