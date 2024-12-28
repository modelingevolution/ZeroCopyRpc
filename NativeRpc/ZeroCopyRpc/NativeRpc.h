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



// This is topic on the server side.
// When the Client subscribes, thread-safe lock-free structures need to be created, that will be used
// In publish thread - which is different that subscribe thread, this is the named-semaphore.
// When client disconnects, we only mark subscription to be disposed on the next iteration of publish loop.

class TopicService {
public:
    struct Subscription
    {
        named_semaphore* Sem = nullptr;
        const char* nSemName;
        int Index = -1;
        bool empty() const
        {
            return Sem == nullptr && Index == -1;
        }
        void Open(const std::string& semName, byte index)
        {
            Sem = new named_semaphore(create_only, nSemName = semName.c_str(), 0);
            Index = index;
            std::cout << "named semaphore created: " << semName << std::endl;
        }

        friend bool operator==(const Subscription& lhs, const Subscription& rhs)
        {
	        return lhs.Sem == rhs.Sem
		        && lhs.Index == rhs.Index;
        }

        friend bool operator!=(const Subscription& lhs, const Subscription& rhs)
        {
	        return !(lhs == rhs);
        }

        Subscription(const std::string& semName, byte index = 0) : Sem(nullptr)
        {
            Sem = new named_semaphore(create_only, nSemName=semName.c_str(),0);
            Index = index;
            std::cout << "named semaphore created: " << semName << std::endl;
        }

        void Close()
        {
            if (Sem != nullptr)  
                Sem->remove(nSemName);
            delete Sem;
            nSemName = nullptr;
            Sem = nullptr;
            Index = -1;
        }

        Subscription() : Sem(nullptr) {}
    };
private:
    
    std::string _channelName;
    std::string _topicName;

    
    // Client Semaphore table
    ConcurrentBag<Subscription,256> _subscriptions;
    IDPool256 _idPool;


    shared_memory_object _shm;
    mapped_region* _region;

	// IN SHM
    // Client PID, Notified, Current Offset table.
    SubscriptionSharedData* _subscribers; // 256
    
    // IN SHM
    CyclicBuffer<1024*1024*8,256>* _buffer;
    
    void NotifyAll()
    {
        
        for(auto i = _subscriptions.begin(); i.is_valid(); i++)
        {
            auto s =i.current_item();
            
            auto& data = _subscribers[s.Index];
            if(data.PendingRemove)
            {
                if (_subscriptions.remove(s))
                {
                    i--;
                    s.Close();
                    this->_idPool.returns(s.Index);
                }
            }
            else 
            {
                if (data.Notified.fetch_add(1) == 0)
                {
                    // this is the first time, need to set the cursors index.
                    data.StartOffset = _buffer->NextIndex() - 1;
                }

                s.Sem->post();
            }
            
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
	    PublishScope(CyclicBuffer<1024 * 1024 * 8, 256>::WriterScope&& w, TopicService* parent) : _scope( std::move(w)), _parent(parent)
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
        TopicService *_parent;
    };
    TopicService(const std::string& channel_name, const std::string& topic_name)
	    : _channelName(channel_name),
	      _topicName(topic_name),
		_subscriptions(),
		
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
    std::string GetSubscriptionSemaphoreName(pid_t pid, int index)
    {
        std::ostringstream oss;
        oss << _channelName << "." << _topicName << "." << pid << "." << (int)index << ".sem";
        std::string semName = oss.str();
        return semName;
    }
    byte Subscribe(pid_t pid)
    {
        byte index = 0;
        if (!this->_idPool.rent(index))
            throw std::exception("Cannot find free id.");

        
        this->_subscribers[index].Reset(pid);
        Subscription s(GetSubscriptionSemaphoreName(pid, index), index);
        _subscriptions.push(s);

        return index;
    }
    PublishScope Prepare(ulong minSize, ulong type)
    {
        return PublishScope(_buffer->WriteScope(minSize,type), this);
    }
    

    ~TopicService() {
        delete _region;
        _shm.remove(_shm.get_name());
    }

    bool Unsubscribe(pid_t pid, byte id) const
    {
        auto &r = this->_subscribers[id];
        if(r.Pid == pid)
        {
            bool expected = false;
	        if(r.PendingRemove.compare_exchange_weak(expected,true))
	        {
		        // operation was successfull
                return true;
	        }
        }
        return false;
    }
};




class SharedMemoryServer {
private:
    std::string _chName;

    std::unordered_map<std::string, TopicService*> _topics;
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
    bool Unsubscribe(const char* topicName, pid_t pid, byte id)
    {
        std::string key(topicName);
        auto it = _topics.find(key);
        if (it != _topics.end())
        {
            // we have found
            it->second->Unsubscribe(pid, id);
        }
        else
        {
            return false;

        }
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
                    rsp.Response.Id = this->Subscribe(env.Request.TopicName, env.Pid);
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
                case 6:
				{
                    auto& env = *(UnSubscribeCommandEnvelope*)buffer;
                    this->Unsubscribe(env.Request.TopicName,  env.Pid, env.Request.Id);
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
    TopicService* OnCreateSubscription(const char *topicName)
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
           shared_memory_object::remove(TopicService::ShmName(this->_chName, topicName).c_str());
           auto result = new TopicService(this->_chName, topicName);
           
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
    TopicService* CreateTopic(const std::string& topicName)
	{
        CreateSubscriptionEnvelope env;
        env.Request.SetTopicName(topicName);

        _messageQueue.send(&env, sizeof(CreateSubscriptionEnvelope), 0);
        
        return env.Response();
    }


};