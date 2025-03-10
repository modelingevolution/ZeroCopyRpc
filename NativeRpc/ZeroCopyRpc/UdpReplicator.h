#pragma once


#include "SharedMemoryClient.h"
#include "SharedMemoryServer.h"
#include <boost/asio.hpp>
#include "Export.h"
#include "UdpReplicationMessages.h"

using boost::asio::ip::udp;
using namespace boost;
using namespace boost::asio;


class EXPORT UdpReplicationSource {
private:
    struct TopicReplicator {
        std::string TopicName;
        std::unique_ptr<ISubscriptionCursor> Cursor;
        std::thread ReplicationThread;
        std::atomic<bool> Running{ true };
        udp::endpoint TargetEndpoint;
    };

    boost::asio::io_context& _io;
    udp::socket _socket;
    SharedMemoryClient _shmClient;
    std::vector<std::shared_ptr<TopicReplicator>> _replicators;
    std::atomic<bool> _running{ true };
    std::mutex _replicatorsMutex;

    void ReplicateLoop(std::shared_ptr<TopicReplicator> replicator);

public:
    UdpReplicationSource(asio::io_context& io,
        const std::string& channelName);

    void ReplicateTopic(const std::string& topicName, const std::string& targetHost,
        uint16_t targetPort);

    ~UdpReplicationSource();
};

class EXPORT UdpReplicationTarget {
private:
    struct TopicReplicator {
        std::string TopicName;
        std::thread ReplicationThread;
        std::atomic<bool> Running{ true };
    };

    asio::io_context& _io;
    udp::socket _socket;
    std::shared_ptr<SharedMemoryServer> _shmServer;
    std::vector<std::shared_ptr<TopicReplicator>> _replicators;
    std::atomic<bool> _running{ true };
    std::mutex _replicatorsMutex;

    void ReplicateLoop(std::shared_ptr<TopicReplicator> replicator);
    void StartReplication(const std::string& topicName);

public:
    UdpReplicationTarget(asio::io_context& io,
        std::shared_ptr<SharedMemoryServer> shmServer,
        std::string &host,
        uint16_t port);

    void ReplicateTopic(const std::string& topicName);
    ~UdpReplicationTarget();
};