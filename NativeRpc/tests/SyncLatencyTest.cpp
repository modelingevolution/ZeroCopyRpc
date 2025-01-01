#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>
#include <iostream>
#include <string>
#include "ThreadSpin.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <boost/interprocess/sync/named_semaphore.hpp>
#include <sys/eventfd.h>
#include <unistd.h>
#include <fcntl.h>
#endif

#include <boost/interprocess/sync/named_semaphore.hpp>

#include "NamedSemaphore.h"

class SyncLatencyTest : public ::testing::Test {
protected:
    static constexpr int FREQUENCY_HZ = 200;
    static constexpr int TEST_DURATION_SEC = 5;
    static constexpr int TOTAL_ITEMS = FREQUENCY_HZ * TEST_DURATION_SEC;

    std::vector<long long> latencies;
    std::atomic<bool> should_stop{ false };
    std::atomic<int> items_processed{ 0 };

    void SetUp() override {
        latencies.resize(TOTAL_ITEMS);
        should_stop = false;
        items_processed = 0;
    }

    void TearDown() override {
        latencies.clear();
    }

    // Timing helper
    inline long long get_time_us() {
        return std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now().time_since_epoch()
        ).count();
    }

    // Statistics calculation
    struct TestStats {
        long long min;
        long long max;
        double avg;
        size_t samples;
    };

    TestStats calculate_statistics() {
        if (latencies.empty()) {
            return { 0, 0, 0.0, 0 };
        }

        long long sum = 0;
        long long min = latencies[0];
        long long max = latencies[0];

        for (long long lat : latencies) {
            sum += lat;
            min = min(min, lat);
            max = max(max, lat);
        }

        return {
            min,
            max,
            static_cast<double>(sum) / latencies.size(),
            latencies.size()
        };
    }
};
#ifdef _WIN32
TEST_F(SyncLatencyTest, NamedSemaphoreLatency) {
    HANDLE hSemaphore = CreateSemaphoreA(
        NULL,           // default security attributes
        0,             // initial count
        TOTAL_ITEMS,   // maximum count
        "TestSem"      // named semaphore
    );

    ASSERT_NE(hSemaphore, nullptr) << "Failed to create semaphore";
    ThreadSpin sw;
    auto producer = std::thread([&]() {
        for (int i = 0; i < TOTAL_ITEMS && !should_stop; ++i) {
            latencies[i] = get_time_us();
            ReleaseSemaphore(hSemaphore, 1, NULL);
            sw.WaitFor(std::chrono::milliseconds(1000 / FREQUENCY_HZ));
        }
        });

    auto consumer = std::thread([&]() {
        while (items_processed < TOTAL_ITEMS && !should_stop) {
            WaitForSingleObject(hSemaphore, INFINITE);
            long long end_time = get_time_us();
            latencies[items_processed] = end_time - latencies[items_processed];
            items_processed++;
        }
        });

    producer.join();
    consumer.join();
    CloseHandle(hSemaphore);

    auto stats = calculate_statistics();

    // Print results
    RecordProperty("Average_Latency_us", stats.avg);
    RecordProperty("Min_Latency_us", stats.min);
    RecordProperty("Max_Latency_us", stats.max);
    RecordProperty("Samples", stats.samples);
    std::cout << "Average_Latency_us: " << stats.avg << std::endl;
    std::cout << "Min_Latency_us: " << stats.min << std::endl;
    std::cout << "Max_Latency_us: " << stats.max << std::endl;
    std::cout << "Samples: " << stats.samples << std::endl;

    // Basic assertions to ensure test ran properly
    EXPECT_EQ(stats.samples, TOTAL_ITEMS);
    EXPECT_GT(stats.avg, 0);
    EXPECT_LT(stats.avg, 1000); // Expected latency under 1ms
}
#else
TEST_F(SyncLatencyTest, NamedSemaphoreLatencyLinux) {
    boost::interprocess::named_semaphore::remove("TestSemaphore");
    boost::interprocess::named_semaphore semaphore(boost::interprocess::create_only, "TestSemaphore", 0);

    ThreadSpin sw;
    auto producer = std::thread([&]() {
        for (int i = 0; i < TOTAL_ITEMS && !should_stop; ++i) {
            latencies[i] = get_time_us();
            semaphore.post();
            sw.Wait(std::chrono::milliseconds(1000 / FREQUENCY_HZ));
        }
        });

    auto consumer = std::thread([&]() {
        while (items_processed < TOTAL_ITEMS && !should_stop) {
            semaphore.wait();
            long long end_time = get_time_us();
            latencies[items_processed] = end_time - latencies[items_processed];
            items_processed++;
        }
        });

    producer.join();
    consumer.join();
    boost::interprocess::named_semaphore::remove("TestSemaphore");

    auto stats = calculate_statistics();

    RecordProperty("Average_Latency_us", stats.avg);
    RecordProperty("Min_Latency_us", stats.min);
    RecordProperty("Max_Latency_us", stats.max);
    RecordProperty("Samples", stats.samples);
    std::cout << "Average_Latency_us: " << stats.avg << std::endl;
    std::cout << "Min_Latency_us: " << stats.min << std::endl;
    std::cout << "Max_Latency_us: " << stats.max << std::endl;
    std::cout << "Samples: " << stats.samples << std::endl;
}
#endif

#ifndef _WIN32
TEST_F(SyncLatencyTest, EventFdLatency) {
    int efd = eventfd(0, 0);
    ASSERT_NE(efd, -1) << "Failed to create eventfd";

    ThreadSpin sw;
    auto producer = std::thread([&]() {
        uint64_t increment = 1;
        for (int i = 0; i < TOTAL_ITEMS && !should_stop; ++i) {
            latencies[i] = get_time_us();
            write(efd, &increment, sizeof(increment));
            sw.Wait(std::chrono::milliseconds(1000 / FREQUENCY_HZ));
        }
        });

    auto consumer = std::thread([&]() {
        uint64_t buffer;
        while (items_processed < TOTAL_ITEMS && !should_stop) {
            read(efd, &buffer, sizeof(buffer));
            long long end_time = get_time_us();
            latencies[items_processed] = end_time - latencies[items_processed];
            items_processed++;
        }
        });

    producer.join();
    consumer.join();
    close(efd);

    auto stats = calculate_statistics();

    RecordProperty("Average_Latency_us", stats.avg);
    RecordProperty("Min_Latency_us", stats.min);
    RecordProperty("Max_Latency_us", stats.max);
    RecordProperty("Samples", stats.samples);
    std::cout << "Average_Latency_us: " << stats.avg << std::endl;
    std::cout << "Min_Latency_us: " << stats.min << std::endl;
    std::cout << "Max_Latency_us: " << stats.max << std::endl;
    std::cout << "Samples: " << stats.samples << std::endl;
}
#endif

TEST_F(SyncLatencyTest, BoostNamedSemaphoreLatency) {
    ThreadSpin sw;
    boost::interprocess::named_semaphore::remove("TestSemaphore");
    boost::interprocess::named_semaphore semaphore(boost::interprocess::create_only, "TestSemaphore", 0);
    auto producer = std::thread([&]() {
        for (int i = 0; i < TOTAL_ITEMS && !should_stop; ++i) {
            latencies[i] = get_time_us();
            semaphore.post(); // Signal the semaphore
            sw.WaitFor(std::chrono::milliseconds(1000 / FREQUENCY_HZ));
        }
        });
    auto consumer = std::thread([&]() {
        while (items_processed < TOTAL_ITEMS && !should_stop) {
            semaphore.wait(); // Wait for the semaphore
            long long end_time = get_time_us();
            latencies[items_processed] = end_time - latencies[items_processed];
            items_processed++;
        }
        });
    producer.join();
    consumer.join();
    boost::interprocess::named_semaphore::remove("TestSemaphore");

    auto stats = calculate_statistics();

    // Print results
    RecordProperty("Average_Latency_us", stats.avg);
    RecordProperty("Min_Latency_us", stats.min);
    RecordProperty("Max_Latency_us", stats.max);
    RecordProperty("Samples", stats.samples);
    std::cout << "Average_Latency_us: " << stats.avg << std::endl;
    std::cout << "Min_Latency_us: " << stats.min << std::endl;
    std::cout << "Max_Latency_us: " << stats.max << std::endl;
    std::cout << "Samples: " << stats.samples << std::endl;

    // Basic assertions to ensure test ran properly
    EXPECT_EQ(stats.samples, TOTAL_ITEMS);
    EXPECT_GT(stats.avg, 0);
    EXPECT_LT(stats.avg, 10000); // Expected latency under 1ms
}
TEST_F(SyncLatencyTest, MeNamedSemaphoreLatency) {
    ThreadSpin sw;
    NamedSemaphore::Remove("TestSemaphore");
    NamedSemaphore semaphore("TestSemaphore", 0);
    auto producer = std::thread([&]() {
        for (int i = 0; i < TOTAL_ITEMS && !should_stop; ++i) {
            latencies[i] = get_time_us();
            semaphore.Release(); // Signal the semaphore
            sw.WaitFor(std::chrono::milliseconds(1000 / FREQUENCY_HZ));
        }
        });
    auto consumer = std::thread([&]() {
        while (items_processed < TOTAL_ITEMS && !should_stop) {
            semaphore.Acquire(); // Wait for the semaphore
            long long end_time = get_time_us();
            latencies[items_processed] = end_time - latencies[items_processed];
            items_processed++;
        }
        });
    producer.join();
    consumer.join();
    boost::interprocess::named_semaphore::remove("TestSemaphore");

    auto stats = calculate_statistics();

    // Print results
    RecordProperty("Average_Latency_us", stats.avg);
    RecordProperty("Min_Latency_us", stats.min);
    RecordProperty("Max_Latency_us", stats.max);
    RecordProperty("Samples", stats.samples);
    std::cout << "Average_Latency_us: " << stats.avg << std::endl;
    std::cout << "Min_Latency_us: " << stats.min << std::endl;
    std::cout << "Max_Latency_us: " << stats.max << std::endl;
    std::cout << "Samples: " << stats.samples << std::endl;

    // Basic assertions to ensure test ran properly
    EXPECT_EQ(stats.samples, TOTAL_ITEMS);
    EXPECT_GT(stats.avg, 0);
    EXPECT_LT(stats.avg, 10000); // Expected latency under 1ms
}
TEST_F(SyncLatencyTest, NamedEventLatency) {
    HANDLE hEvent = CreateEventA(
        NULL,     // default security attributes
        FALSE,    // auto-reset event
        FALSE,    // initial state is nonsignaled
        "TestEvent"
    );

    ASSERT_NE(hEvent, nullptr) << "Failed to create event";
    ThreadSpin sw;
    auto producer = std::thread([&]() {
        for (int i = 0; i < TOTAL_ITEMS && !should_stop; ++i) {
            latencies[i] = get_time_us();
            SetEvent(hEvent);
            sw.WaitFor(std::chrono::milliseconds(1000 / FREQUENCY_HZ));
        }
        });

    auto consumer = std::thread([&]() {
        while (items_processed < TOTAL_ITEMS && !should_stop) {
            WaitForSingleObject(hEvent, INFINITE);
            long long end_time = get_time_us();
            latencies[items_processed] = end_time - latencies[items_processed];
            items_processed++;
        }
        });

    producer.join();
    consumer.join();
    CloseHandle(hEvent);

    auto stats = calculate_statistics();

    // Print results
    RecordProperty("Average_Latency_us", stats.avg);
    RecordProperty("Min_Latency_us", stats.min);
    RecordProperty("Max_Latency_us", stats.max);
    RecordProperty("Samples", stats.samples);

    std::cout << "Average_Latency_us: " << stats.avg << std::endl;
    std::cout << "Min_Latency_us: " << stats.min << std::endl;
    std::cout << "Max_Latency_us: " << stats.max << std::endl;
    std::cout << "Samples: " << stats.samples << std::endl;

    // Basic assertions to ensure test ran properly
    EXPECT_EQ(stats.samples, TOTAL_ITEMS);
    EXPECT_GT(stats.avg, 0);
    EXPECT_LT(stats.avg, 1000); // Expected latency under 1ms
}
