#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <SharedMemoryServer.h>
#include <SharedMemoryClient.h>
#include <Random.h>
#include <algorithm>
#include <boost/uuid/detail/md5.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/algorithm/hex.hpp>

#include "PeriodicTimer.h"
#include "StopWatch.h"
#include "ThreadSpin.h"
#include "ZeroCopyRpcException.h"

using namespace std::chrono;
using namespace std;

class SharedMemoryServerTest : public ::testing::Test {
protected:
	SharedMemoryServer* srv = nullptr;
	SharedMemoryClient* client = nullptr;

	inline static void ClearPreviousStuff()
	{
		message_queue::remove("Foo");
		if (TopicService::TryRemove("Foo", "Boo"))
		{
			cout << "SHM removed" << endl;
			this_thread::sleep_for(milliseconds(100)); // The file needs to be removed.
		}
	}

	void TearDown() override
	{
		try {
			delete client;
			delete srv;
		}
		catch (std::exception &e)
		{
			std::cout << e.what() << std::endl;
		}
	}
};

TEST_F(SharedMemoryServerTest, StartAndStopOneTopic)
{
	ClearPreviousStuff();
	srv = new SharedMemoryServer("Foo");

	srv->CreateTopic("Boo");

	delete srv;
	srv = nullptr;

	// We expect the communication channel to be open, topics were not removed
	EXPECT_TRUE(message_queue::remove("Foo"));
	EXPECT_FALSE(shared_memory_object::remove("Foo.Boo"));
};

TEST_F(SharedMemoryServerTest, StartAndStopFullClean)
{
	// To make it independent.
	ClearPreviousStuff();

	srv = new SharedMemoryServer("Foo");

	srv->CreateTopic("Boo");
	auto response = srv->RemoveTopic("Boo");
	
	delete srv;
	srv = nullptr;

	EXPECT_TRUE(response);
	// We expect the communication channel to be closed, topics were removed
	EXPECT_FALSE(message_queue::remove("Foo"));
	EXPECT_FALSE(shared_memory_object::remove("Foo.Boo"));
};

struct Message
{
	ulong value;
	std::chrono::time_point<std::chrono::high_resolution_clock> timestamp;
	Message(ulong v) : value(v), timestamp(std::chrono::high_resolution_clock::now()) {}
};
TEST_F(SharedMemoryServerTest, SubscribePublish)
{
	ClearPreviousStuff();

	srv = new SharedMemoryServer("Foo");
	TopicService* topic = srv->CreateTopic("Boo");
	client = new SharedMemoryClient("Foo");
	client->Connect();

	// subscribe operation, makes all following publish operation readable for cursor.
	auto cursor = client->Subscribe("Boo");

	auto msgType = Random::NextUlong() % 255;
	auto msgValue = Random::NextUlong();

	topic->Publish<Message>(msgType, msgValue);

	auto accessor = cursor->Read();

	EXPECT_EQ(accessor.Item->Type, msgType);
	auto msg = accessor.As<Message>();
	EXPECT_EQ(msg->value, msgValue);

	auto receivedTime = std::chrono::high_resolution_clock::now();
	auto duration = std::chrono::duration_cast<std::chrono::microseconds>(receivedTime - msg->timestamp).count();
	std::cout << "Duration: " << duration << " µs" << std::endl;

	// an attempt to read again, should fail, because we hadn't published twice, but once.
	auto second = cursor->TryRead(accessor);
	EXPECT_FALSE(second);

	// subscription unsubscribe is done at destructor of the cursor.
	
}

TEST_F(SharedMemoryServerTest, TwoReadersScenario) {

	ClearPreviousStuff();

	srv = new SharedMemoryServer("Foo");
	TopicService* topic = srv->CreateTopic("Boo");

	// Create two separate clients
	client = new SharedMemoryClient("Foo");
	
	client->Connect();

	// Subscribe both clients
	auto cursor1 = client->Subscribe("Boo");
	auto cursor2 = client->Subscribe("Boo");

	// Publish a message
	auto msgType = Random::NextUlong() % 255;
	auto msgValue = Random::NextUlong();
	topic->Publish<Message>(msgType, msgValue);

	// Read from both cursors
	auto accessor1 = cursor1->Read();
	auto accessor2 = cursor2->Read();

	// Verify both readers got the same message
	EXPECT_EQ(accessor1.Item->Type, msgType);
	EXPECT_EQ(accessor2.Item->Type, msgType);

	auto msg1 = accessor1.As<Message>();
	auto msg2 = accessor2.As<Message>();
	EXPECT_EQ(msg1->value, msgValue);
	EXPECT_EQ(msg2->value, msgValue);

}

TEST_F(SharedMemoryServerTest, LateSubscriptionScenario) {
	ClearPreviousStuff();

	srv = new SharedMemoryServer("Foo");
	TopicService* topic = srv->CreateTopic("Boo");
	client = new SharedMemoryClient("Foo");
	client->Connect();

	// Publish first message before subscription
	auto msgType1 = Random::NextUlong() % 255;
	auto msgValue1 = Random::NextUlong();
	topic->Publish<Message>(msgType1, msgValue1);

	// Subscribe after first publish
	auto cursor = client->Subscribe("Boo");

	// Publish second message after subscription
	auto msgType2 = Random::NextUlong() % 255;
	auto msgValue2 = Random::NextUlong();
	topic->Publish<Message>(msgType2, msgValue2);

	// Read message - should get only the second message
	auto accessor = cursor->Read();
	EXPECT_EQ(accessor.Item->Type, msgType2);
	auto msg = accessor.As<Message>();
	EXPECT_EQ(msg->value, msgValue2);

	// Verify no more messages are available
	auto second = cursor->TryRead(accessor);
	EXPECT_FALSE(second);
}
template<unsigned int TSize>
struct BigFrame
{
	uuid Hash;
	unsigned char Data[TSize];
	BigFrame()
	{
		static std::mt19937_64 rng(std::random_device{}());
		std::uniform_int_distribution<uint64_t> dist(0, UINT64_MAX);

		// Fill Data with random 64-bit chunks
		uint64_t* dataAsUlong = reinterpret_cast<uint64_t*>(Data);
		size_t numChunks = TSize / sizeof(uint64_t);

		for (size_t i = 0; i < numChunks; ++i) {
			dataAsUlong[i] = dist(rng);
		}

		Hash = computeHash(Data, TSize);
	}
private:
	static uuid computeHash(const unsigned char* data, size_t size)
	{
		// Create an MD5 object
		boost::uuids::detail::md5 hash;
		// Process the data
		hash.process_bytes(data, size);
		// Retrieve the hash result
		boost::uuids::detail::md5::digest_type digest;
		hash.get_digest(digest);
		// Convert the digest to a UUID
		uuid result;
		memcpy(&result, &digest, sizeof(digest));
		if (sizeof(digest) != sizeof(uuid))
			throw ZeroCopyRpcException("Fuck");
		return result;
	}
};
//typedef BigFrame<32768> MyBigFrame;
typedef BigFrame<1920*1080*3/2> MyBigFrame;

TEST_F(SharedMemoryServerTest, LargeMessageCountScenario) {
	ThreadSpin sp;
	ClearPreviousStuff();
	srv = new SharedMemoryServer("Foo");
	TopicService* topic = srv->CreateTopic("Boo");
	client = new SharedMemoryClient("Foo");
	client->Connect();

	auto msgType = Random::NextUlong() % 255;

	//const size_t messageCount = 256 * 2; // Two complete cycles
	const size_t messageCount = 60; // Two complete cycles

	std::vector<uuid> published_messages(messageCount);
	std::vector<bool> received(messageCount);
	std::vector<StopWatch> delay(messageCount);

	auto consumer = std::thread([&]() {
		size_t read_count = 0;
		auto cursor = client->Subscribe("Boo");
		while (read_count < messageCount) {
			auto accessor = cursor->Read();
			//std::cout << "r: " << read_count << '\n';
			auto msg = accessor.As<MyBigFrame>();
			if (published_messages[read_count] == msg->Hash)
				received[read_count] = true;
			delay[read_count].Stop();

			read_count++;
			EXPECT_EQ(accessor.Item->Type, msgType);
		}
		});
	std::this_thread::sleep_for(std::chrono::milliseconds(100));

	// Publish messages
	StopWatch s;
	s.Start();
	PeriodicTimer _timer = PeriodicTimer::CreateFromFrequency(18);
	for (size_t i = 0; i < messageCount; i++) {
		_timer.WaitForNext();

		auto scope = topic->Prepare(sizeof(MyBigFrame), msgType);
		auto& span = scope.Span();
		auto ptr = new (span.Start) MyBigFrame();
		span.Commit(sizeof(MyBigFrame));

		published_messages[i] = ptr->Hash;
		delay[i].Start();
		//std::cout << "w: " << i << '\n';
	}
	std::cout << "Waiting for consumer thread to finish up." << '\n';
	consumer.join();
	s.Stop();

	// Read and verify messages
	int okCount = 0;
	std::chrono::nanoseconds delayTotal(0);
	for (int i = 0; i < messageCount; i++)
	{
		if (received[i]) ++okCount;
		delayTotal += delay[i].Total();
		//cout << delay[i].Total();
	}
	EXPECT_EQ(okCount, messageCount) << "Expected to have received " << messageCount << " hashes but received only: " << okCount;


	ulong size = messageCount * sizeof(MyBigFrame);
	auto transfer = size / s.ElapsedSeconds();
	auto fps = messageCount / s.ElapsedSeconds();
	microseconds avgDelay = duration_cast<microseconds>(delayTotal / messageCount);

	cout << "Transfer:  " << transfer/1024/1024 << " MB/sec" << endl;
	cout << "Fps:       " << fps << " Hz" << endl;
	cout << "Avg.Delay: " << avgDelay.count() << " µs" << endl;
}
TEST_F(SharedMemoryServerTest, HighPrecisionProducerConsumerPerformance) {
	ClearPreviousStuff();
	srv = new SharedMemoryServer("Foo");
	TopicService* topic = srv->CreateTopic("Boo");
	client = new SharedMemoryClient("Foo");
	client->Connect();

	struct PerformanceMessage {
		uint64_t sequence;
		std::chrono::nanoseconds expected_publish_time;
		std::chrono::high_resolution_clock::time_point actual_publish_time;

		PerformanceMessage(uint64_t seq, std::chrono::nanoseconds expected)
			: sequence(seq), expected_publish_time(expected) {
		}
	};

	// Performance metrics
	struct Metrics {
		std::atomic<uint64_t> messages_produced{ 0 };
		std::atomic<uint64_t> messages_consumed{ 0 };
		std::atomic<uint64_t> total_publish_latency_ns{ 0 };
		std::atomic<uint64_t> total_consume_latency_ns{ 0 };
		std::atomic<uint64_t> max_publish_latency_ns{ 0 };
		std::atomic<uint64_t> max_consume_latency_ns{ 0 };
		std::atomic<uint64_t> publish_timing_errors{ 0 };  // Count of times we missed the 100µs target
	} metrics;

	auto cursor = client->Subscribe("Boo");
	std::atomic<bool> running{ true };

	// Producer thread
	auto producer = std::thread([&]() {
		const auto start_time = std::chrono::high_resolution_clock::now();
		const auto end_time = start_time + std::chrono::seconds(5);
		const auto target_interval = std::chrono::microseconds(100);
		uint64_t sequence = 0;

		while (std::chrono::high_resolution_clock::now() < end_time) {
			auto expected_publish_time = start_time + (sequence * target_interval);

			// Spin-wait until we're close to the target time
			while (std::chrono::high_resolution_clock::now() < expected_publish_time) {
				ThreadSpin::Wait(10); // CPU hint that we're spin-waiting
			}

			auto msg = PerformanceMessage(sequence,
				std::chrono::duration_cast<std::chrono::nanoseconds>(expected_publish_time - start_time));
			msg.actual_publish_time = std::chrono::high_resolution_clock::now();

			topic->Publish<PerformanceMessage>(1, msg);

			// Calculate publish timing accuracy
			auto publish_latency = std::chrono::duration_cast<std::chrono::nanoseconds>(
				msg.actual_publish_time - expected_publish_time).count();

			metrics.total_publish_latency_ns.fetch_add(publish_latency);
			metrics.max_publish_latency_ns.store(
				max(metrics.max_publish_latency_ns.load(), static_cast<uint64_t>(publish_latency)));

			if (publish_latency > 1000) { // 1 microsecond threshold
				metrics.publish_timing_errors.fetch_add(1);
			}

			metrics.messages_produced.fetch_add(1);
			sequence++;
		}
		});

	// Consumer thread (current thread)
	auto accessor = cursor->Read();
	const auto consumer_start = std::chrono::high_resolution_clock::now();

	bool r = true;
	while (std::chrono::high_resolution_clock::now() < consumer_start + std::chrono::seconds(5)) {
		if (r) {
			auto msg = accessor.As<PerformanceMessage>();
			auto consume_time = std::chrono::high_resolution_clock::now();

			// Calculate end-to-end latency
			auto consume_latency = std::chrono::duration_cast<std::chrono::nanoseconds>(
				consume_time - msg->actual_publish_time).count();

			metrics.total_consume_latency_ns.fetch_add(consume_latency);
			metrics.max_consume_latency_ns.store(
				max(metrics.max_consume_latency_ns.load(), static_cast<uint64_t>(consume_latency)));

			metrics.messages_consumed.fetch_add(1);
		}

		r = cursor->TryRead(accessor);
	}

	producer.join();

	// Calculate and report metrics
	auto messages_produced = metrics.messages_produced.load();
	auto messages_consumed = metrics.messages_consumed.load();
	auto avg_publish_latency_ns = metrics.total_publish_latency_ns.load() / messages_produced;
	auto avg_consume_latency_ns = metrics.total_consume_latency_ns.load() / messages_consumed;
	auto max_publish_latency_ns = metrics.max_publish_latency_ns.load();
	auto max_consume_latency_ns = metrics.max_consume_latency_ns.load();
	auto timing_errors = metrics.publish_timing_errors.load();

	std::cout << "\nPerformance Test Results:" << std::endl;
	std::cout << "Messages produced: " << messages_produced << std::endl;
	std::cout << "Messages consumed: " << messages_consumed << std::endl;
	std::cout << "Message rate: " << (messages_produced / 5.0) << " msg/sec" << std::endl;
	std::cout << "Average publish latency: " << avg_publish_latency_ns << " ns" << std::endl;
	std::cout << "Maximum publish latency: " << max_publish_latency_ns << " ns" << std::endl;
	std::cout << "Average consume latency: " << avg_consume_latency_ns << " ns" << std::endl;
	std::cout << "Maximum consume latency: " << max_consume_latency_ns << " ns" << std::endl;
	std::cout << "Publish timing errors: " << timing_errors <<
		" (" << (timing_errors * 100.0 / messages_produced) << "%)" << std::endl;

	// Basic assertions to ensure the test ran correctly
	EXPECT_GT(messages_produced, 0);
	EXPECT_GT(messages_consumed, 0);
	EXPECT_LE(messages_consumed, messages_produced);
	EXPECT_GT((messages_produced / 5.0), 9000); // Expect at least 9k messages per second
	EXPECT_LT(avg_consume_latency_ns, 1000000); // Expect average latency under 1ms
}