#include "UdpReplicator.h"

#include <boost/log/trivial.hpp>

#include "UdpFrameDefragmentator.h"
#include "ZeroCopyRpcException.h"
#include "UdpFrameProcessor.h"

void UdpReplicationSource::ReplicateLoop(std::shared_ptr<TopicReplicator> replicator) {
    while (replicator->Running && _running) {
        CyclicBuffer::Accessor msg;

        while (!replicator->Cursor->TryReadFor(msg, chrono::seconds(5)))
            if (!replicator->Running || !_running)
                return;
        auto now = std::chrono::steady_clock::now();
        auto duration = now.time_since_epoch();
        UdpFrameIterator<1500> iterator(msg.Get(), msg.Size(), msg.Type(), duration.count());

        while(iterator.CanRead())

        try {
            auto buffers = *iterator;
            // Create a scatter/gather array for sending both header and data in one datagram

            // Send both parts as a single datagram
            _socket.send_to(buffers, replicator->TargetEndpoint);
            //BOOST_LOG_TRIVIAL(debug) << "Sent [" << replicator->TargetEndpoint.address().to_string() << ":" << replicator->TargetEndpoint.port() << "]: " << (buffers[0].size() + msg.Size()) << "B, message-size: " << msg.Size() << " msg-type: " << msg.Type();
            ++iterator;
        }
        catch (const boost::system::system_error& e) {
            BOOST_LOG_TRIVIAL(error) << "Failed to send UDP datagram: " << e.what();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

UdpReplicationSource::UdpReplicationSource(asio::io_context& io,
    const std::string& channelName
   )
    : _io(io)
    , _socket(io, udp::endpoint(udp::v4(), 0))  // Bind to any port
    , _shmClient(channelName) {

    _shmClient.Connect();
}
udp::endpoint ResolveUdpEndpoint(const std::string& host, uint16_t port, boost::asio::io_context& io_context) {
    // Create a resolver
    udp::resolver resolver(io_context);
    // Resolve the host and port
    udp::resolver::results_type endpoints = resolver.resolve(
        udp::v4(), // Use IPv4 (or use udp::v6() for IPv6)
        host,
        std::to_string(port)
    );
    // Return the first resolved endpoint
    return *endpoints.begin();
}
void UdpReplicationSource::ReplicateTopic(const std::string& topicName, const std::string& targetHost,
    uint16_t targetPort) {
    auto replicator = std::make_shared<TopicReplicator>();
    replicator->TopicName = topicName;
    replicator->Cursor = _shmClient.Subscribe(topicName);
    replicator->TargetEndpoint = ResolveUdpEndpoint(targetHost, targetPort, _io);
   
    try 
    {
        {
            std::lock_guard lock(_replicatorsMutex);
            _replicators.push_back(replicator);
        }

        replicator->ReplicationThread = std::thread([this, replicator]() {
            ReplicateLoop(replicator);
            });
        replicator->ReplicationThread.detach();
    }
    catch (const boost::system::system_error& e) {
        BOOST_LOG_TRIVIAL(error) << "Failed to send topic subscription: " << e.what();
        throw;
    }
}

void UdpReplicationTarget::ReplicateLoop(std::shared_ptr<TopicReplicator> replicator) {
    auto topic = _shmServer->CreateTopic(replicator->TopicName);
    
    UdpFrameDefragmentator defragmentator(*topic->GetBuffer(), 1500);
    std::vector<byte> buffer(topic->MaxMessageSize());
    while (replicator->Running && _running) {
        try {
			udp::endpoint sender_endpoint;
			
			size_t bytesReceived = _socket.receive_from(asio::buffer(buffer), sender_endpoint);
			auto ret = defragmentator.ProcessFragment(buffer.data(), bytesReceived);

			if (bytesReceived < sizeof(UdpReplicationMessageHeader))
				throw ZeroCopyRpcException("Replication message incomplete.");

			if (ret)
				topic->NotifyAll();


			
		}
		catch (const boost::system::system_error& e) {
			BOOST_LOG_TRIVIAL(error) << "UDP receive error: " << e.what();
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

UdpReplicationTarget::UdpReplicationTarget(asio::io_context& io,
    std::shared_ptr<SharedMemoryServer> shmServer,
    std::string &host, uint16_t port)
    : _io(io)
    , _socket(io, ResolveUdpEndpoint(host, port,io))
    , _shmServer(shmServer) {
}

void UdpReplicationTarget::ReplicateTopic(const std::string& topicName) {
    auto replicator = std::make_shared<TopicReplicator>();
    replicator->TopicName = topicName;

    {
        std::lock_guard lock(_replicatorsMutex);
        _replicators.push_back(replicator);
    }

    replicator->ReplicationThread = std::thread([this, replicator]() {
        ReplicateLoop(replicator);
        });
    replicator->ReplicationThread.detach();
}

UdpReplicationTarget::~UdpReplicationTarget() {
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

UdpReplicationSource::~UdpReplicationSource() {
    _running = false;

    std::lock_guard lock(_replicatorsMutex);
    for (auto& replicator : _replicators) {
        replicator->Running = false;
    }

    boost::system::error_code ec;
    _socket.close(ec);
}