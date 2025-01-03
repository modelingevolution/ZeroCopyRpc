#include "ShmReplicator.h"

#include "ZeroCopyRpcException.h"

void ShmReplicationSource::AcceptLoop() {
	while (_running) {
		try {
			auto socket = std::make_shared<tcp::socket>(_io);
			_acceptor.accept(*socket);

			std::thread([this, socket]() {
				HandleNewClient(socket);
				}).detach();
		}
		catch (const boost::system::system_error& e) {
			auto error_code = e.code();
			if (error_code == asio::error::operation_aborted ||
				error_code == asio::error::interrupted)
				return;
			throw;
		}
	}
}
void ShmReplicationSource::HandleNewClient(std::shared_ptr<tcp::socket> socket) {
	try {
		{
			std::lock_guard lock(_clientsMutex);
			_clientTopics[socket] = std::vector<std::shared_ptr<TopicReplicator>>();
		}
		while (_running) {
			HandleReplicateSubscription(socket);
		}
	}
	catch (...) {
		std::lock_guard lock(_clientsMutex);
		_clientTopics.erase(socket);
	}
}
void ShmReplicationSource::HandleReplicateSubscription(std::shared_ptr<tcp::socket> socket) {
	ReplicateTopicMessage header;
	asio::read(*socket, asio::buffer(&header, sizeof(header)));

	std::vector<char> topicNameBuffer(header.TopicNameLength);
	asio::read(*socket, asio::buffer(topicNameBuffer.data(), header.TopicNameLength));

	std::string topicName(topicNameBuffer.data(), header.TopicNameLength);

	auto replicator = std::make_shared<TopicReplicator>();
	replicator->TopicName = topicName;
	replicator->Cursor = _shmClient.Subscribe(topicName);
	{
		std::lock_guard lock(_clientsMutex);
		_clientTopics[socket].push_back(replicator);
	}
	replicator->ReplicationThread = std::thread([this, socket, replicator]() {
		ReplicateLoop(socket, replicator);
		});
	replicator->ReplicationThread.detach();
}
void ShmReplicationSource::ReplicateLoop(std::shared_ptr<tcp::socket> socket,
	std::shared_ptr<TopicReplicator> replicator) {

	while (replicator->Running && _running) {
		CyclicBuffer::Accessor msg;

		while (!replicator->Cursor->TryReadFor(msg, chrono::seconds(5)))
			if (!replicator->Running || !_running)
				return;

		ReplicationMessage header;
		header.Size = msg.Size();
		header.Type = msg.Type();

		try {
			asio::write(*socket, asio::buffer(&header, sizeof(header)));
			asio::write(*socket, asio::buffer(msg.Get(), msg.Size()));
		}
		catch (...) {
			replicator->Running = false;
			std::lock_guard lock(_clientsMutex);
			_clientTopics.erase(socket);
			return;
		}
	}
}

ShmReplicationSource::ShmReplicationSource(asio::io_context& io,
	const std::string& channelName, uint16_t port)
	: _io(io)
	, _acceptor(io, tcp::endpoint(tcp::v4(), port))
	, _shmClient(channelName) {

	_shmClient.Connect();

	std::thread acceptThread([this]() { AcceptLoop(); });
	acceptThread.detach();
}

ShmReplicationSource::~ShmReplicationSource() {
	_running = false;
	_acceptor.close();

	std::lock_guard lock(_clientsMutex);
	for (auto& replicators : _clientTopics | std::views::values) {
		for (auto& replicator : replicators) {
			replicator->Running = false;
		}
	}
}

void ShmReplicationTarget::ReplicateLoop(std::shared_ptr<TopicReplicator> replicator) {
	auto topic = _shmServer->CreateTopic(replicator->TopicName);

	while (replicator->Running && _running) {
		try {
			ReplicationMessage header;
			auto rhs = asio::read(_socket, asio::buffer(&header, sizeof(header)));
			if (rhs != sizeof(ReplicationMessage))
				throw ZeroCopyRpcException("Replication header message incomplete");

			auto scope = topic->Prepare(header.Size, header.Type);
			auto& span = scope.Span();

			auto ms = asio::read(_socket, asio::buffer(span.Start, header.Size));

			if (ms != header.Size)
				throw ZeroCopyRpcException("Replication message incomplete");

			span.Commit(header.Size);
		}
		catch (const boost::system::system_error& e) {
			auto error_code = e.code();
			if (error_code == asio::error::eof ||
				error_code == asio::error::connection_reset ||
				error_code == asio::error::connection_aborted)
				return;
			throw;
		}
	}
}

void ShmReplicationTarget::StartReplication(const std::string& topicName) {
	ReplicateTopicMessage msg;
	msg.TopicNameLength = static_cast<uint32_t>(topicName.length());

	asio::write(_socket, asio::buffer(&msg, sizeof(msg)));
	asio::write(_socket, asio::buffer(topicName.data(), topicName.length()));
}

void ShmReplicationTarget::ReplicateTopic(const std::string& topicName) {
	auto replicator = std::make_shared<TopicReplicator>();
	replicator->TopicName = topicName;

	{
		std::lock_guard lock(_replicatorsMutex);
		_replicators.push_back(replicator);
	}

	StartReplication(topicName);

	replicator->ReplicationThread = std::thread([this, replicator]() {
		ReplicateLoop(replicator);
		});
	replicator->ReplicationThread.detach();
}

ShmReplicationTarget::ShmReplicationTarget(asio::io_context& io,
	std::shared_ptr<SharedMemoryServer> shmServer,
	const std::string& host, uint16_t port)
	: _io(io)
	, _socket(io)
	, _shmServer(shmServer) {

	_socket.connect(tcp::endpoint(
		asio::ip::make_address(host), port));
}

ShmReplicationTarget::~ShmReplicationTarget() {
	_running = false;

	{
		std::lock_guard lock(_replicatorsMutex);
		for (auto& replicator : _replicators) {
			replicator->Running = false;
		}
	}

	boost::system::error_code ec;
	_socket.close(ec);
}