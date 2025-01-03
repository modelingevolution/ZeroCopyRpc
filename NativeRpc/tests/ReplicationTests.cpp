#include <gtest/gtest.h>
#include "ShmReplicator.h"
#include <thread>
#include <future>

#include "StopWatch.h"
#include "ThreadSpin.h"

struct TestMessage {
    int64_t Value;
    char Data[256];
};

class ReplicationTest : public ::testing::Test {
protected:
    asio::io_context io;
    std::thread ioThread;
    std::unique_ptr<SharedMemoryServer> sourceServer;
    std::shared_ptr<SharedMemoryServer> targetReplicaServer;
    std::unique_ptr<ShmReplicationSource> master;
    std::unique_ptr<ShmReplicationTarget> slave;

    const std::string CH_SOURCE = "source";
    const std::string CH_REPLICA = "repl";
    const std::string TOPIC_NAME = "test_topic";

    void SetUp() override {
        // Clean existing shared memory
        message_queue::remove(CH_SOURCE.c_str());
        message_queue::remove(CH_REPLICA.c_str());
        if (TopicService::TryRemove(CH_SOURCE, TOPIC_NAME))
            std::cout << "source.test_topic was removed." << std::endl;
        if(TopicService::TryRemove(CH_REPLICA, TOPIC_NAME))
            std::cout << "replica.test_topic was removed." << std::endl;


        // Start IO thread
        ioThread = std::thread([this]() { io.run(); });

        // Create source server for publishing test messages
        sourceServer = std::make_unique<SharedMemoryServer>(CH_SOURCE);
        sourceServer->CreateTopic(TOPIC_NAME);

        // Setup replication
        master = std::make_unique<ShmReplicationSource>(io, CH_SOURCE,  5555);
        std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Allow master to start

        targetReplicaServer = std::make_shared<SharedMemoryServer>(CH_REPLICA);
        slave = std::make_unique<ShmReplicationTarget>(io, targetReplicaServer, "127.0.0.1", 5555);
        slave->ReplicateTopic(TOPIC_NAME);

        std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Allow slave to connect
    }

    void TearDown() override {
        master.reset();
        slave.reset();
        sourceServer.reset();
        targetReplicaServer.reset();

        io.stop();
        if (ioThread.joinable()) {
            ioThread.join();
        }
    }
};

TEST_F(ReplicationTest, ReplicatesSingleMessage) {
    // Prepare test message
    TestMessage msg{ 42, "test data" };

    // Get topic and publish
    auto topic = sourceServer->CreateTopic(TOPIC_NAME);
    
    // Setup client to read from replica
    SharedMemoryClient client(CH_REPLICA);
    client.Connect();

    auto cursor = client.Subscribe(TOPIC_NAME);

    StopWatch sw = StopWatch::StartNew();
    topic->Publish<TestMessage>(1, msg);

    
    // Read and verify
    CyclicBuffer::Accessor data;
    auto until = chrono::steady_clock::now() + chrono::seconds(1);
    while(! cursor->TryRead(data) && chrono::steady_clock::now() < until)
    {
        ThreadSpin::Wait(500);
    }
    sw.Stop();
    std::cout << (data.IsValid() ? "Successful": "Unsuccessful") << " TCP Roundtrip in: " << sw.ElapsedMilliseconds() << " ms" << std::endl;
    ASSERT_TRUE(data.IsValid());
    if (!data.IsValid()) return;

    ASSERT_EQ(data.Size(), sizeof(TestMessage));
    ASSERT_EQ(data.Type(), 1);

    auto* received = reinterpret_cast<const TestMessage*>(data.Get());
    EXPECT_EQ(received->Value, 42);
    EXPECT_STREQ(received->Data, "test data");
}

TEST_F(ReplicationTest, ReplicatesMultipleMessages) {
    auto topic = sourceServer->CreateTopic(TOPIC_NAME);

    // Publish multiple messages
    const int MSG_COUNT = 100;
    std::vector<int64_t> values;
    for (int i = 0; i < MSG_COUNT; i++) {
        values.push_back(i);
        TestMessage msg{ static_cast<int64_t>(i), "data" };
        topic->Publish<TestMessage>(1, msg);
    }

    // Read from replica
    SharedMemoryClient client(CH_REPLICA);
    client.Connect();
    auto cursor = client.Subscribe(TOPIC_NAME);

    // Verify all messages
    for (int i = 0; i < MSG_COUNT; i++) {
        auto data = cursor->Read();
        auto* received = reinterpret_cast<const TestMessage*>(data.Get());
        EXPECT_EQ(received->Value, values[i]);
    }
}

TEST_F(ReplicationTest, HandlesSlaveDisconnection) {
    auto topic = sourceServer->CreateTopic(TOPIC_NAME);

    // Publish initial message
    TestMessage msg1{ 1, "before disconnect" };
    topic->Publish<TestMessage>(1, msg1);

    // Disconnect and reconnect slave
    slave.reset();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    targetReplicaServer = std::make_shared<SharedMemoryServer>(CH_REPLICA);
    slave = std::make_unique<ShmReplicationTarget>(io, targetReplicaServer,  "127.0.0.1", 5555);
    slave->ReplicateTopic(TOPIC_NAME);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Publish after reconnect
    TestMessage msg2{ 2, "after reconnect" };
    topic->Publish<TestMessage>(1, msg2);

    // Verify message received after reconnection
    SharedMemoryClient client(CH_REPLICA);
    client.Connect();
    auto cursor = client.Subscribe(TOPIC_NAME);

    auto data = cursor->Read();
    auto* received = reinterpret_cast<const TestMessage*>(data.Get());
    EXPECT_EQ(received->Value, 2);
    EXPECT_STREQ(received->Data, "after reconnect");
}