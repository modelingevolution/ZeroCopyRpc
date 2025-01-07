#include "SharedMemoryClient.h"
#include <boost/log/trivial.hpp>
#include "ThreadSpin.h"
#include "ZeroCopyRpcException.h"


void SharedMemoryClient::Callback::On(void* msg) const
{
	Func(msg, State);
}

SharedMemoryClient::Callback::Callback(void* state, const std::function<void(void*, void*)>& func): State(state),
	Func(func)
{
}

SharedMemoryClient::Callback::Callback(): State(nullptr), Func(nullptr)
{

}

void SharedMemoryClient::InvokeUnsubscribe(const std::string& topicName, byte sloth)
{
	UnSubscribeCommandEnvelope env;
	env.Request.SetTopicName(topicName);
	env.Request.SlothId = sloth;

	auto delegate = std::bind(&SharedMemoryClient::OnUnsubscribed, this, std::placeholders::_1, std::placeholders::_2);
	std::promise<UnSubscribeResponseEnvelope*> promise;
	Callback c(&promise, delegate);
	_messages.InsertOrUpdate(env.CorrelationId, c);
	_srvQueue.send(&env, sizeof(UnSubscribeCommandEnvelope), 0);
	auto value = promise.get_future().get();
	delete value;
	// we don't do anything here, beside we need to wait. work is done at cursor and topic level.
}

SharedMemoryClient::Topic::Topic(SharedMemoryClient* parent, const std::string& topicName):
Name(topicName), Parent(parent),
_openCursorServerCount(0),
_openCursorClientCount(0)
{
	Shm = new boost::interprocess::shared_memory_object(open_only, ShmName().c_str(), read_only);
	Region = new mapped_region(*Shm, read_only);
	auto base = Region->get_address();
	Metadata = (TopicMetadata*)base;
	Subscribers = (SubscriptionSharedData*)Metadata->SubscribersTableAddress(base);
	
	SharedBuffer = new CyclicBuffer((byte*)Metadata->BufferAddress(base));
	
}

SharedMemoryClient::Topic::~Topic()
{
	SharedBuffer = nullptr;
	Subscribers = nullptr;
	Metadata = nullptr;
	delete Region;
	Shm->remove(Shm->get_name());
	delete Shm;
}

void SharedMemoryClient::Topic::Unsubscribe(byte sloth)
{
	this->_openCursorClientCount.fetch_sub(1);
	_openSlots.erase(std::ranges::remove(_openSlots, sloth).begin(), _openSlots.end());

	this->Parent->InvokeUnsubscribe(this->Name, sloth);
	// most likely we should clean up if this was the last subscription.
}

void SharedMemoryClient::Topic::AckUnsubscribed(byte sloth)
{
	this->_openCursorServerCount.fetch_sub(1, std::memory_order::relaxed);
}

void SharedMemoryClient::Topic::UnsubscribeAll()
{
	std::vector<byte> slotsToUnsubscribe = _openSlots;
	for (auto sloth : slotsToUnsubscribe)
	{
		Unsubscribe(sloth);
	}
}

std::string SharedMemoryClient::Topic::ShmName() const
{
	std::ostringstream oss;
	oss << Parent->_chName << "." << Name << ".buffer";
	return oss.str();
}

void SharedMemoryClient::DispatchResponses()
{
	byte buffer[1024];
	size_t recSize;
	uint priority;
	uuid& correlationId = *((uuid*)(buffer + sizeof(ulong)));
	ulong& msgType = *((ulong*)buffer);
	bool canceled = false;
	while (!canceled)
	{
		auto timeout = std::chrono::time_point<std::chrono::high_resolution_clock>::clock::now();
		timeout = timeout + std::chrono::seconds(30);
		if (_clientQueue.timed_receive(buffer, 1024, recSize, priority, timeout))
		{
			if (msgType == 0)
			{
				canceled = true;
				break;
			}
			Callback c;
			if(_messages.TryGetValue(correlationId, c))
				c.On(buffer);
		}
		else
			BOOST_LOG_TRIVIAL(debug) << "No messages has been received at dispatcher client thread.";
	}
}

void SharedMemoryClient::OnHelloReceived(void* buffer, void* promise)
{
	auto ptr = (HelloResponseEnvelope*)buffer;
	auto p = (std::promise<HelloResponseEnvelope*>*)promise;
	p->set_value(new HelloResponseEnvelope(*ptr)); // default copy-ctor;
	_messages.Remove(ptr->CorrelationId);
}

void SharedMemoryClient::OnSubscribed(void* buffer, void* promise)
{
	auto ptr = (SubscribeResponseEnvelope*)buffer;
	auto p = (std::promise<SubscribeResponseEnvelope*>*)promise;
	p->set_value(new SubscribeResponseEnvelope(*ptr)); // default copy-ctor;
	_messages.Remove(ptr->CorrelationId);
}

void SharedMemoryClient::OnUnsubscribed(void* buffer, void* promise)
{
	auto ptr = (UnSubscribeResponseEnvelope*)buffer;
	auto p = (std::promise<UnSubscribeResponseEnvelope*>*)promise;
	p->set_value(new UnSubscribeResponseEnvelope(*ptr)); // default copy-ctor;
	_messages.Remove(ptr->CorrelationId);
	if (ptr->Response.IsSuccess) {
		auto topic = this->Get(ptr->Response.TopicName);
		topic->AckUnsubscribed(ptr->Response.SlothId);
	}
}
SharedMemoryClient::Topic* SharedMemoryClient::Get(const std::string& topic)
{
	auto it = _topics.find(topic);
	Topic* t = nullptr;
	if (it == _topics.end())
	{
		return nullptr;
	}
	else
	{
		t = it->second;
	}
	return t;
}
SharedMemoryClient::Topic* SharedMemoryClient::GetOrCreate(const std::string& topic)
{
	auto it = _topics.find(topic);
	Topic* t = nullptr;
	if (it == _topics.end())
	{
		// we need to create new.
		t = new Topic(this, topic);
		_topics.emplace(topic, t);
	}
	else
	{
		t = it->second;
	}
	return t;
}

SharedMemoryClient::SubscriptionCursor::SubscriptionCursor(byte sloth, Topic* topic): _sem(nullptr), _sloth(sloth), _topic(topic), _cursor(nullptr)
{
	_sem = new NamedSemaphore( SemaphoreName(), NamedSemaphore::OpenMode::Open);
	
	_topic->_openCursorClientCount.fetch_add(1);
	_topic->_openCursorServerCount.fetch_add(1);
	_topic->_openSlots.push_back(sloth);
}

std::string SharedMemoryClient::SubscriptionCursor::SemaphoreName() const
{
	std::ostringstream oss;
	oss << this->_topic->Parent->_chName << "." << _topic->Name << "." << (ulong)getCurrentProcessId() << "." << (int)_sloth << ".sem";
	return oss.str();
}

CyclicBuffer::Accessor SharedMemoryClient::SubscriptionCursor::Read()
{
	// WARNING: IF YOU CHANGE THIS METHOD, you need to change 2 more TryRead and Read
	_sem->Acquire();

	if(_cursor == nullptr)
	{
		auto value = _topic->Subscribers[_sloth].NextIndex.load(); // this should the value from shared memory.
		BOOST_LOG_TRIVIAL(debug) << "Loaded next cursor value: " << value;
		_cursor = new CyclicBuffer::Cursor(_topic->SharedBuffer->OpenCursor(value)); // move ctor.
	}
	for(int i = 0; i < 50; i++)
	{
		if (_cursor->TryRead())
		{
			BOOST_LOG_TRIVIAL(debug) << "Waited " << (i * 200) << "CPU cycles before memory was in sync";
			return _cursor->Data();
		}
		ThreadSpin::Wait(200);
		//std::cout << "Data in SHM not yet ready.\n";
	}
	throw ZeroCopyRpcException("TryRead returned false.");
}

bool SharedMemoryClient::SubscriptionCursor::TryRead(CyclicBuffer::Accessor &a) 
{
	// WARNING: IF YOU CHANGE THIS METHOD, you need to change 2 more TryRead and Read
	if (!_sem->TryAcquire())
		return false;

	if (_cursor == nullptr)
	{
		auto value = _topic->Subscribers[_sloth].NextIndex.load(); // this should the value from shared memory.
		BOOST_LOG_TRIVIAL(debug) << "Loaded next cursor value: " << value;
		_cursor = new CyclicBuffer::Cursor(_topic->SharedBuffer->OpenCursor(value)); // move ctor.
	}

	for (int i = 0; i < 50; i++)
	{
		if (_cursor->TryRead())
		{
			BOOST_LOG_TRIVIAL(debug) << "Waited " << (i * 200) << "CPU cycles before memory was in sync";
			a = std::move(_cursor->Data());
			return true;
		}
		ThreadSpin::Wait(200);
	}
	throw ZeroCopyRpcException("TryRead returned false.");
}
SharedMemoryClient::SubscriptionCursor::SubscriptionCursor(SubscriptionCursor&& other) noexcept: //ISubscriptionCursor(std::move(other)),
	_sem(other._sem),
	_sloth(other._sloth),
	_topic(other._topic),
	_cursor(other._cursor)
{
	other._cursor = nullptr;
	other._sem = nullptr;
	other._topic = nullptr;
}

SharedMemoryClient::SubscriptionCursor& SharedMemoryClient::SubscriptionCursor::operator=(SubscriptionCursor other)
{
	using std::swap;
	swap(*this, other);
	return *this;
}



SharedMemoryClient::SubscriptionCursor::~SubscriptionCursor()
{
	if (_sem != nullptr) {
		this->_topic->Unsubscribe(this->_sloth);
		delete _sem;
		delete _cursor;
		NamedSemaphore::Remove(SemaphoreName());
		_sem = nullptr;
		_cursor = nullptr;
		_topic = nullptr;
		_sloth = 255; // last slot, will fail faster.
	}
}

std::string SharedMemoryClient::ClientQueueName(const std::string& channelName)
{
	std::ostringstream oss;
	oss << channelName << "." << getCurrentProcessId();
	return oss.str();
}


SharedMemoryClient::SharedMemoryClient(const std::string& channelName):
	_chName(channelName),
	_srvQueue(open_or_create, channelName.c_str(), 256,1024),
	_clientQueue(create_only, ClientQueueName(channelName).c_str(), 256, 1024)
{
	
	this->_dispatcher = std::thread([this]() { DispatchResponses(); });
}

template<typename T>
std::chrono::milliseconds RequestDuration(const T& response) 
{
	auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::high_resolution_clock::now() - response.RequestCreated);
	return duration;
}

bool SharedMemoryClient::SubscriptionCursor::TryReadFor(
	CyclicBuffer::Accessor& a,
	const std::chrono::milliseconds& timeout)
{
	// WARNING: IF YOU CHANGE THIS METHOD, you need to change 2 more TryRead and Read
	if (!_sem->TryAcquireFor(timeout)) {
		return false;
	}

	if (_cursor == nullptr) {
		auto value = _topic->Subscribers[_sloth].NextIndex.load();
		BOOST_LOG_TRIVIAL(debug) << "Loaded next cursor value: " << value;
		_cursor = new CyclicBuffer::Cursor(_topic->SharedBuffer->OpenCursor(value));
	}

	for (int i = 0; i < 50; i++) {
		if (_cursor->TryRead()) {
			if(i > 0)
				BOOST_LOG_TRIVIAL(debug) << "Waited " << (i * 200) << " CPU cycles before memory was in sync";
			a = std::move(_cursor->Data());
			return true;
		}
		ThreadSpin::Wait(200);
	}

	throw ZeroCopyRpcException("TryRead returned false.");
}
void SharedMemoryClient::Connect()
{
	HelloCommandEnvelope env;
	auto delegate = std::bind(&SharedMemoryClient::OnHelloReceived, this, std::placeholders::_1, std::placeholders::_2);
	std::promise<HelloResponseEnvelope*> promise;
	Callback c(&promise, delegate);
	_messages.InsertOrUpdate(env.CorrelationId, c);
	_srvQueue.send(&env, sizeof(HelloCommandEnvelope), 0);
	auto value = std::unique_ptr<HelloResponseEnvelope>(promise.get_future().get());
	BOOST_LOG_TRIVIAL(info) << "Connection established successfully. [" << RequestDuration(value->Response) << "]";
}

std::unique_ptr<ISubscriptionCursor> SharedMemoryClient::Subscribe(const std::string& topicName)
{
	SubscribeCommandEnvelope env;
	auto delegate = std::bind(&SharedMemoryClient::OnSubscribed, this, std::placeholders::_1, std::placeholders::_2);
	env.Request.SetTopicName(topicName);

	std::promise<SubscribeResponseEnvelope*> promise;
	Callback c(&promise, delegate);
	_messages.InsertOrUpdate(env.CorrelationId, c);

	//std::cout << "Sending message size: " << sizeof(SubscribeCommandEnvelope) << std::endl;
	_srvQueue.send(&env, sizeof(SubscribeCommandEnvelope), 0);

	auto value = promise.get_future().get();

	auto topic = GetOrCreate(topicName);
	auto result = std::make_unique<SubscriptionCursor>(value->Response.Id, topic);

	delete value;
	return result;
}

SharedMemoryClient::~SharedMemoryClient()
{
	auto topics = _topics;
	for(auto b = topics.begin(); b!= topics.end(); ++b)
	{
		b->second->UnsubscribeAll();
		_topics.erase(b->first);
	}
	ulong msg = 0;
	_clientQueue.send(&msg, sizeof(ulong), 0);
	_dispatcher.join();

	message_queue::remove(ClientQueueName(this->_chName).c_str());
	
}

void swap(SharedMemoryClient::SubscriptionCursor& lhs, SharedMemoryClient::SubscriptionCursor& rhs) noexcept
{
	using std::swap;
	swap(lhs._sem, rhs._sem);
	swap(lhs._sloth, rhs._sloth);
	swap(lhs._topic, rhs._topic);
	swap(lhs._cursor, rhs._cursor);
}
