#include "SharedMemoryClient.h"

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

void SharedMemoryClient::Unsubscribe(const std::string& topicName, byte sloth)
{
	UnSubscribeCommandEnvelope env;
	env.Request.SetTopicName(topicName);

	auto delegate = std::bind(&SharedMemoryClient::OnUnsubscribed, this, std::placeholders::_1, std::placeholders::_2);
	std::promise<UnSubscribeResponseEnvelope*> promise;
	Callback c(&promise, delegate);
	_messages.InsertOrUpdate(env.CorrelationId, c);
	_srvQueue.send(&env, sizeof(UnSubscribeCommandEnvelope), 0);
	auto value = promise.get_future().get();
	// we don't do anything here, beside we need to wait. work is done at cursor and topic level.
}

SharedMemoryClient::Topic::Topic(SharedMemoryClient* parent, const std::string& topicName): Name(topicName), Parent(parent)
{
	Shm = new boost::interprocess::shared_memory_object(open_only, ShmName().c_str(), read_only);
	Region = new mapped_region(*Shm, read_only);
	auto base = Region->get_address();
	Metadata = (TopicMetadata*)base;
	Subscribers = (SubscriptionSharedData*)Metadata->SubscribersTableAddress(base);
	SharedBuffer = (CyclicBuffer<1024 * 1024 * 8, 256>*)Metadata->BuffserAddress(base);
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
	this->Parent->Unsubscribe(this->Name, sloth);
	// most likely we should clean up if this was the last subscription.
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
}

SharedMemoryClient::Topic* SharedMemoryClient::GetOrCreate(const std::string& topic)
{
	auto it = _subscriptions.find(topic);
	Topic* t = nullptr;
	if (it == _subscriptions.end())
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

SharedMemoryClient::SubscriptionCursor::SubscriptionCursor(byte sloth, Topic* topic): _sem(nullptr), _sloth(sloth), _topic(topic), _cursor(nullptr)
{
	_sem = new named_semaphore(open_only, SemaphoreName().c_str());
	auto value = _topic->Subscribers[sloth].StartOffset;
	_cursor = new CyclicBuffer<1024 * 1024 * 8, 256>::Cursor(_topic->SharedBuffer->ReadNext(value)); // move ctor.
}

std::string SharedMemoryClient::SubscriptionCursor::SemaphoreName() const
{
	std::ostringstream oss;
	oss << this->_topic->Parent->_chName << "." << _topic->Name << "." << (ulong)GetCurrentProcessId() << "." << (int)_sloth << ".sem";
	return oss.str();
}

CyclicBuffer<1024 * 1024 * 8, 256>::Accessor SharedMemoryClient::SubscriptionCursor::Read()
{
	_sem->wait();
	if (_cursor->TryRead())
		return _cursor->Data();
	else throw std::exception("TryRead returned false.");
}

SharedMemoryClient::SubscriptionCursor::SubscriptionCursor(SubscriptionCursor&& other) noexcept: _sem(other._sem),
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
		named_semaphore::remove(SemaphoreName().c_str());
		_sem = nullptr;
		_cursor = nullptr;
		_topic = nullptr;
		_sloth = 255; // last slot, will fail faster.
	}
}

SharedMemoryClient::SharedMemoryClient(const std::string& channelName):
	_chName(channelName),
	_srvQueue(open_only, channelName.c_str()),
	_clientQueue(create_only, (channelName + "." + std::to_string(getCurrentProcessId())).c_str(), 256, 1024)
{
	std::thread([this]() { DispatchResponses(); }).detach();
}

void SharedMemoryClient::Connect()
{
	HelloCommandEnvelope env;
	auto delegate = std::bind(&SharedMemoryClient::OnHelloReceived, this, std::placeholders::_1, std::placeholders::_2);
	std::promise<HelloResponseEnvelope*> promise;
	Callback c(&promise, delegate);
	_messages.InsertOrUpdate(env.CorrelationId, c);
	_srvQueue.send(&env, sizeof(HelloCommandEnvelope), 0);
	auto value = promise.get_future().get();
	delete value;
	auto duration = std::chrono::high_resolution_clock::now() - value->Response.From;
	std::cout << "Connected. It took: " << duration << std::endl;

}

std::unique_ptr<SharedMemoryClient::SubscriptionCursor> SharedMemoryClient::Subscribe(const std::string& topicName)
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
	auto result = std::make_unique<SubscriptionCursor>(value->Response.Id, topic);

	delete value;
	return result;
}

void swap(SharedMemoryClient::SubscriptionCursor& lhs, SharedMemoryClient::SubscriptionCursor& rhs) noexcept
{
	using std::swap;
	swap(lhs._sem, rhs._sem);
	swap(lhs._sloth, rhs._sloth);
	swap(lhs._topic, rhs._topic);
	swap(lhs._cursor, rhs._cursor);
}
