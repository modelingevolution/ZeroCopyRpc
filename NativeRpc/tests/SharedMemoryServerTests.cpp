#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <SharedMemoryServer.h>
#include <SharedMemoryClient.h>
#include <Random.h>

class SharedMemoryServerTest : public ::testing::Test {
protected:
	SharedMemoryServer* srv = nullptr;
	SharedMemoryClient* client = nullptr;

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
	message_queue::remove("Foo");
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
	message_queue::remove("Foo");

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
	message_queue::remove("Foo");
	TopicService::ClearIfExists("Foo", "Boo");
	srv = new SharedMemoryServer("Foo");
	TopicService* topic = srv->CreateTopic("Boo");
	client = new SharedMemoryClient("Foo");
	client->Connect();
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

	auto second = cursor->TryRead(accessor);
	EXPECT_FALSE(second);
}
