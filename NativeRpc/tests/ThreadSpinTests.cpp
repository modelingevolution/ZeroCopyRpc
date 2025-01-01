#include <gtest/gtest.h>
#include <thread>

#include "ThreadSpin.h"

class ThreadSpinTest : public ::testing::Test {
protected:
    ThreadSpin spinner;
    const double TIMING_TOLERANCE = 0.01; // 2% tolerance for timing tests
};

TEST_F(ThreadSpinTest, StaticWaitExecutesForSpecifiedCycles) {
    int cycles = 1000;
    auto start = std::chrono::steady_clock::now();
    ThreadSpin::Wait(cycles);
    auto duration = std::chrono::steady_clock::now() - start;

    EXPECT_GT(duration, std::chrono::nanoseconds(0));
}

TEST_F(ThreadSpinTest, WaitRespectsDuration) {
    auto target_duration = std::chrono::milliseconds(1);
    auto start = std::chrono::steady_clock::now();

    spinner.WaitFor(target_duration);

    auto actual_duration = std::chrono::steady_clock::now() - start;
    auto duration_ms = std::chrono::duration_cast<std::chrono::nanoseconds>(actual_duration).count();
    auto target_ms = std::chrono::duration_cast<std::chrono::nanoseconds>(target_duration).count();

    EXPECT_NEAR(duration_ms, target_ms, target_ms * TIMING_TOLERANCE);
    std::cout << "Actual difference: " << std::abs(duration_ms-target_ms)*100/target_ms << "%" << std::endl;
}

TEST_F(ThreadSpinTest, WaitDoesNotUnderrun) {
    auto target_duration = std::chrono::milliseconds(5);
    auto start = std::chrono::steady_clock::now();

    spinner.WaitFor(target_duration);

    auto actual_duration = std::chrono::steady_clock::now() - start;
    EXPECT_GE(actual_duration, target_duration);
}

TEST_F(ThreadSpinTest, HandlesZeroDuration) {
    auto start = std::chrono::steady_clock::now();
    spinner.WaitFor(std::chrono::milliseconds(0));
    auto duration = std::chrono::steady_clock::now() - start;

    EXPECT_LT(duration, std::chrono::milliseconds(1));
}

TEST_F(ThreadSpinTest, HandlesSmallDurations) {
    auto target_duration = std::chrono::microseconds(100);
    auto start = std::chrono::steady_clock::now();

    spinner.WaitFor(target_duration);

    auto actual_duration = std::chrono::steady_clock::now() - start;
    EXPECT_GE(actual_duration, target_duration);
}

TEST_F(ThreadSpinTest, MultipleConcurrentSpinners) {
    const int THREAD_COUNT = 4;
    std::vector<std::thread> threads;
    std::vector<std::chrono::nanoseconds> durations(THREAD_COUNT);

    auto target_duration = std::chrono::milliseconds(1);

    for (int i = 0; i < THREAD_COUNT; ++i) {
        threads.emplace_back([i, target_duration, &durations] {
            ThreadSpin local_spinner;
            auto start = std::chrono::steady_clock::now();
            local_spinner.WaitFor(target_duration);
            durations[i] = std::chrono::steady_clock::now() - start;
            });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    for (const auto& duration : durations) {
        EXPECT_GE(duration, target_duration);
    }
}