#include "SharedMemoryServer.h"

#include <boost/log/trivial.hpp>

#include "ProcessUtils.h"
#include "ZeroCopyRpcException.h"


void TopicService::Subscription::OpenOrCreate(const std::string& semName, byte index)
{
	if (this->Name != nullptr)
	{
		Close();
	}

	this->Name = new std::string(semName);
	this->Sem = new NamedSemaphore(semName, NamedSemaphore::OpenMode::OpenOrCreate, 0);
	this->Index = index;
	//std::cout << "named semaphore created: " << semName << std::endl;
}

TopicService::Subscription::Subscription(const std::string& semName, byte index): Sem(nullptr), Name(nullptr)
{
	this->Name = new std::string(semName);
	this->Sem = new NamedSemaphore( semName, NamedSemaphore::OpenMode::Create, 0);
	this->Index = index;
	//std::cout << "named semaphore created: " << semName << std::endl;
}

void TopicService::Subscription::Close()
{
	if (Sem != nullptr)
		NamedSemaphore::Remove(*Name);
	delete Sem;
	delete Name;
	Name = nullptr;
	Sem = nullptr;
	Index = -1;
}

TopicService::Subscription::Subscription(): Sem(nullptr), Name(nullptr)
{

}

void TopicService::NotifyAll()
{
	std::array<Subscription,256> toRemove;
	int ix = 0;
	for(auto i = _subscriptions.begin(); i.is_valid(); i++)
	{
		auto s =i.current_item();
            
		auto& data = _subscribers[s.Index];
		if(data.PendingRemove)
		{
			toRemove[ix++] = s;
		}
		else 
		{
			if (data.Notified.fetch_add(1) == 0)
			{
				// this is the first time, need to set the cursors index.
				data.NextIndex = _buffer->NextIndex();
				//std::cout << "SERVER: Start offset set: " << data.NextIndex << std::endl;
			}

			s.Sem->Release();
		}
            
	}
	for(--ix;ix >= 0;--ix)
	{
		auto s = toRemove[ix];
		if (_subscriptions.remove(s))
		{
			auto& data = _subscribers[s.Index];
			//i--;
			s.Close();
			data.PendingRemove.store(false);
			data.Active.store(false);
			this->_idPool.returns(s.Index);
		}
	}
}

std::string TopicService::ShmName(const std::string& channel_name, const std::string& topic_name)
{
	return channel_name + "." + topic_name + ".buffer";
}

PublishScope::PublishScope(CyclicBuffer::WriterScope&& w, TopicService* parent): _scope( std::move(w)), _parent(parent)
{
		    
}

ulong PublishScope::Type() const
{ return _scope.Type; }

PublishScope::PublishScope(PublishScope&& other) noexcept: _scope(std::move(other._scope))
{
	other._parent = nullptr;
}

PublishScope::~PublishScope()
{
	if(_parent != nullptr && _scope.Span.CommitedSize() > 0)
	{
		_parent->NotifyAll();
		_parent = nullptr;
	}
}


void TopicService::RemoveDanglingSubscriptionEntry(int i, SubscriptionSharedData& sub) const
{
	auto semName = GetSubscriptionSemaphoreName(sub.Pid, i);
	NamedSemaphore::Remove(semName);
	sub.PendingRemove.store(false);
}
bool TopicService::ClearIfExists(const std::string& channel_name, const std::string& topic_name, unsigned int messageCount, unsigned int bufferSize)
{
	try {
		shared_memory_object shm(open_only, ShmName(channel_name, topic_name).c_str(), read_write);
		offset_t size;
		shm.get_size(size);
		TopicMetadata m = {
			CyclicBuffer::SizeOf(messageCount, bufferSize),
			sizeof(SubscriptionSharedData) * 256,
		messageCount,
			bufferSize };

		if (size > 0)
		{
			if (size != m.TotalSize())
				shm.truncate(m.TotalSize());

			mapped_region region(shm, read_write);
			auto ptr = region.get_address();
			memset(ptr, 0, size);

			TopicMetadata* metadata = (TopicMetadata*)ptr;
			*metadata = m; // copy
			region.flush();
			return true;
		}
		return false;
	}
	catch (boost::interprocess::interprocess_exception &e)
	{
		return false;
	}
}

bool TopicService::TryRemove(const std::string& channel_name, const std::string& topic_name)
{
	auto name = ShmName(channel_name, topic_name);
	return shared_memory_object::remove(name.c_str());
}

TopicService::TopicService(const std::string& channel_name, const std::string& topic_name, 
                           unsigned int messageCount, unsigned int bufferSize) :
	_channelName(channel_name),
	_topicName(topic_name),
	_shm(nullptr),
	_region(nullptr)
{
	_shm = new shared_memory_object(open_or_create, ShmName(channel_name, topic_name).c_str(), read_write);

	TopicMetadata m = {
		CyclicBuffer::SizeOf(messageCount,bufferSize),
		sizeof(SubscriptionSharedData) * 256,
		messageCount,
		bufferSize};
	offset_t size;
	_shm->get_size(size);

	if (size == 0) {
		_shm->truncate(m.TotalSize());

		_region = new mapped_region(*_shm, read_write);
		auto dst = _region->get_address();
		memset(dst, 0, m.TotalSize());

		TopicMetadata* metadata = (TopicMetadata*)dst;
		*metadata = m; // copy

		_subscribers = (SubscriptionSharedData*)m.SubscribersTableAddress(dst);
		_buffer = new CyclicBuffer(static_cast<byte*>(m.BufferAddress(dst)),messageCount, bufferSize);
	}
	else
	{
		BOOST_LOG_TRIVIAL(info) << "Channel's '" << channel_name << "' shared memory buffer for topic " << topic_name << " found, we'll reuse it.";
		_region = new mapped_region(*_shm, read_write);
		auto dst = _region->get_address();
		auto& m = *(TopicMetadata*)dst;
		_subscribers = (SubscriptionSharedData*)m.SubscribersTableAddress(dst);

		_buffer = new CyclicBuffer(static_cast<byte*>(m.BufferAddress(dst)));
		
		if(_buffer->Unlock())
		{
			BOOST_LOG_TRIVIAL(warning) << "Buffer was unlocked.";
		}
		// now we should rebuild Subscribers table.
		for(int i = 0; i < 256; i++)
		{
			auto& sub = _subscribers[i];
			if(sub.Active.load(std::memory_order::relaxed) )
			{
				if(sub.PendingRemove.load(std::memory_order_relaxed))
				{
					// remove dangling resources.
					RemoveDanglingSubscriptionEntry(i, sub);
				}
				else
				{
					// let's check if there is process with pid
					if(!is_process_running(sub.Pid))
					{
						// the process is not running, we shall clean up resources.
						RemoveDanglingSubscriptionEntry(i, sub);
					}
					else
					{
						byte index = static_cast<byte>(i);
						// We need to rebuild the subscription entry.
						if (!this->_idPool.try_rent(index))
							throw ZeroCopyRpcException("Cannot rebuild subscription.");

						Subscription s;
						s.OpenOrCreate(GetSubscriptionSemaphoreName(sub.Pid, i), index);
						this->_subscriptions.push(s);
					}
				}
			}
		}
	}
}



std::string TopicService::GetSubscriptionSemaphoreName(pid_t pid, int index) const
{
	std::ostringstream oss;
	oss << _channelName << "." << _topicName << "." << pid << "." << (int)index << ".sem";
	std::string semName = oss.str();
	return semName;
}

byte TopicService::Subscribe(pid_t pid)
{
	byte index = 0;
	if (!this->_idPool.rent(index))
		throw ZeroCopyRpcException("Cannot find free id.");

	auto& item = this->_subscribers[index];
	item.Reset(pid);
	//Subscription s(GetSubscriptionSemaphoreName(pid, index), index);
	Subscription s;
	s.OpenOrCreate(GetSubscriptionSemaphoreName(pid, index), index);
	_subscriptions.push(s);

	return index;
}

PublishScope TopicService::Prepare(ulong minSize, ulong type)
{
	return PublishScope(_buffer->WriteScope(minSize,type), this);
}

TopicService::~TopicService()
{
	delete _buffer;
	_buffer = nullptr;
	delete _region;
	_region = nullptr;
	
	if (_shm != nullptr)
	{
		if(this->_subscriptions.empty())
			shared_memory_object::remove(_shm->get_name());
		delete _shm;
		_shm = nullptr;
	}
	
}

bool TopicService::Unsubscribe(pid_t pid, byte id) const
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

std::string TopicService::Name()
{
	return this->_topicName;
}

CyclicMemoryPool::Span& PublishScope::Span()
{
	CyclicMemoryPool::Span &p  = _scope.Span; return p;
}

byte SharedMemoryServer::Subscribe(const char* topicName, pid_t pid)
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

bool SharedMemoryServer::OnUnsubscribe(const char* topicName, pid_t pid, byte id)
{
	std::string key(topicName);
	auto it = _topics.find(key);
	if (it != _topics.end())
	{
		// we have found
		return it->second->Unsubscribe(pid, id);
	}
	return false;
}

message_queue* SharedMemoryServer::GetClient(pid_t pid)
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

void SharedMemoryServer::OnHelloResponse(pid_t pid, std::chrono::time_point<std::chrono::high_resolution_clock> now,
	const uuid& correlationId)
{
	message_queue* m = GetClient(pid);
	HelloResponseEnvelope env;
	env.CorrelationId = correlationId;
	env.Response.RequestCreated = now;
	m->send(&env, sizeof(HelloResponseEnvelope), 0);

}

void SharedMemoryServer::DispatchMessages()
{
	byte buffer[1024];
	size_t recSize;
	uint priority;
	ulong& messageType = *((ulong*)buffer);
	bool canceled = false;
	while(!canceled)
	{
		auto timeout = std::chrono::time_point<std::chrono::high_resolution_clock>::clock::now();
		timeout = timeout + std::chrono::seconds(30);
		if (_messageQueue.timed_receive(buffer, 1024, recSize, priority, timeout))
		{
			switch(messageType)
			{
			case 0:
				canceled = true;
				break;
			case 1:
				{
					auto& env = *(SubscribeCommandEnvelope*)buffer;
					BOOST_LOG_TRIVIAL(debug) << "Handling subscribe to topic from PID: " << env.Pid << ", " << env.Request;
					SubscribeResponseEnvelope rsp;
					rsp.CorrelationId = env.CorrelationId;
					rsp.Response.Id = this->Subscribe(env.Request.TopicName, env.Pid);
					if(!GetClient(env.Pid)->try_send(&rsp, sizeof(SubscribeResponseEnvelope), 0))
					{
						BOOST_LOG_TRIVIAL(error) << "Cannot send message to client.";
					}
					BOOST_LOG_TRIVIAL(info) << "Topic '" << env.Request.TopicName << "' subscribed from PID: " << env.Pid;
					break;
				}
			case 2:
				{
					auto& env= *(CreateSubscriptionEnvelope*)buffer;
					auto &rqt = env.Request;
					BOOST_LOG_TRIVIAL(debug) << "Handling CreateTopic command: " << rqt;
					env.Set(this->OnCreateTopic(rqt.TopicName, rqt.MaxMessageCount, rqt.BufferSize));
					break;
				}
			case 3:
				{
					auto& env = *(HelloCommandEnvelope*)buffer;
					BOOST_LOG_TRIVIAL(debug) << "Handling HelloCommand from PID: " << env.Pid;
					this->OnHelloResponse(env.Pid, env.Request.Created, env.CorrelationId);
					break;
				}
			case 6:
				{
					auto& env = *(UnSubscribeCommandEnvelope*)buffer;
					
					message_queue* m = GetClient(env.Pid);
					if (m == nullptr)
					{
						BOOST_LOG_TRIVIAL(error) << "Cannot find message queue for client PID: " << env.Pid;
						continue;
					}

					UnSubscribeResponseEnvelope rsp;
					rsp.CorrelationId = env.CorrelationId;
					std::string tmp = env.Request.TopicName;
					rsp.Response.SetTopicName(tmp);
					rsp.Response.IsSuccess = this->OnUnsubscribe(env.Request.TopicName, env.Pid, env.Request.SlothId);
					rsp.Response.SlothId = env.Request.SlothId;
					m->send(&rsp, sizeof(UnSubscribeResponseEnvelope), 0);

					break;
				}
			case 8:
				{
				auto& env = *(RemoveSubscriptionEnvelope*)buffer;
				BOOST_LOG_TRIVIAL(info) << "Remove subscription, " << env.Request;
				env.Set(this->RemoveSubscription(env.Request.TopicName));
				break;
				}
			
			default:
				break;
			}
		}
		else
			BOOST_LOG_TRIVIAL(debug) << "No messages has been received at server dispatcher thread.";
	}
	BOOST_LOG_TRIVIAL(debug) << "Shared memory server's dispatcher thread exited.";
}

bool SharedMemoryServer::RemoveSubscription(const char* topicName)
{
	std::string name = topicName;
	auto it = _topics.find(name);
	if (it != _topics.end())
	{
		// we have found
		delete it->second;
		_topics.erase(it);

		return true;
	}
	else
	{
		return false;
	}
}
TopicService* SharedMemoryServer::OnCreateTopic(const char* topicName, unsigned int messageCount, unsigned int bufferSize)
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
		auto result = new TopicService(this->_chName, topicName, messageCount, bufferSize);
           
		_topics.emplace(topicName, result);
		return result;
	}
	//delete promise;
}

SharedMemoryServer::SharedMemoryServer(const std::string& channel): _chName(channel),
_messageQueue(open_or_create, channel.c_str(), 256, 1024)
{
	this->dispatcher = std::thread([this]() { DispatchMessages(); });
}

SharedMemoryServer::~SharedMemoryServer()
{
	// exit command
	ulong buffer[1];
	buffer[0] = 0;
	
	this->_messageQueue.send(buffer, sizeof(ulong), 0);

	if(this->dispatcher.joinable())
		this->dispatcher.join();

	for (const auto& t : _topics | std::views::values)
	{
		delete t;
	}

	// If there are no topics, we can safely remote communication channel.
	if (_topics.empty())
		message_queue::remove(this->_chName.c_str());

	_topics.clear();
	BOOST_LOG_TRIVIAL(info) << "Shared Memory Server closed.";
}

bool SharedMemoryServer::RemoveChannel(const std::string& channel)
{
	return message_queue::remove(channel.c_str());
}

bool SharedMemoryServer::RemoveTopic(const std::string& topicName)
{
	RemoveSubscriptionEnvelope env;
	env.Request.SetTopicName(topicName);

	_messageQueue.send(&env, sizeof(RemoveSubscriptionEnvelope), 0);

	return env.Response();
}
TopicService* SharedMemoryServer::CreateTopic(const std::string& topicName, unsigned int messageCount, unsigned int bufferSize)
{
	CreateSubscriptionEnvelope env;
	env.Request.SetTopicName(topicName);
	env.Request.MaxMessageCount = messageCount;
	env.Request.BufferSize = bufferSize;
	_messageQueue.send(&env, sizeof(CreateSubscriptionEnvelope), 0);
        
	return env.Response();
}

bool operator==(const TopicService::Subscription& lhs, const TopicService::Subscription& rhs)
{
	return lhs.Sem == rhs.Sem
		&& lhs.Index == rhs.Index;
}

bool operator!=(const TopicService::Subscription& lhs, const TopicService::Subscription& rhs)
{
	return !(lhs == rhs);
}
