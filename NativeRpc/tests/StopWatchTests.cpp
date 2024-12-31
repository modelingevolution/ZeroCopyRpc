#include "StopWatch.h"
#include <gtest/gtest.h>
#include <thread>
#include <cmath> // For std::abs

constexpr double EPSILON = 0.001; // Precision for floating-point comparisons

TEST(StopWatchTest, DefaultConstructor) {
    StopWatch sw;
    EXPECT_NEAR(sw.ElapsedMilliseconds(), 0.0, EPSILON);
    EXPECT_FALSE(sw.Total().count() > 0);
}

TEST(StopWatchTest, StartAndStop) {
    StopWatch sw;
    sw.Start();
    auto start = std::chrono::steady_clock::now();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    auto end = std::chrono::steady_clock::now();
    auto elapsedManual = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    auto elapsed = sw.Stop().count();
    EXPECT_GT(elapsed, 0);
    EXPECT_NEAR(elapsed, elapsedManual, elapsedManual * 0.2); // Allow 20% margin for timing variations
}


TEST(StopWatchTest, Reset) {
    StopWatch sw;
    sw.Start();
    auto start = std::chrono::steady_clock::now();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    auto end = std::chrono::steady_clock::now();
    sw.Reset();
    EXPECT_NEAR(sw.ElapsedMilliseconds(), 0.0, EPSILON);

    auto elapsedManual = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    EXPECT_GT(elapsedManual, 0);
}

TEST(StopWatchTest, ElapsedMilliseconds) {
    StopWatch sw;
    sw.Start();
    auto start = std::chrono::steady_clock::now();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    auto end = std::chrono::steady_clock::now();

    auto elapsedManual = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    auto elapsed = sw.ElapsedMilliseconds();
    EXPECT_NEAR(elapsed, elapsedManual / 1e6, 0.2); // Convert nanoseconds to milliseconds
}

TEST(StopWatchTest, ElapsedSeconds) {
    StopWatch sw;
    
    auto start = std::chrono::steady_clock::now();
    sw.Start();

    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    auto end = std::chrono::steady_clock::now();

    auto elapsed = sw.ElapsedSeconds();
    auto elapsedManual = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count() / 1e9;
    
    EXPECT_NEAR(elapsed, elapsedManual, 0.1); // Convert nanoseconds to seconds
}

TEST(StopWatchTest, StartNew) {
    StopWatch sw = StopWatch::StartNew();
    auto start = std::chrono::steady_clock::now();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    auto end = std::chrono::steady_clock::now();

    auto elapsedManual = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    auto elapsed = sw.Stop().count();
    EXPECT_GT(elapsed, 0);
    EXPECT_NEAR(elapsed, elapsedManual, elapsedManual * 0.2); // Allow 20% margin
}



TEST(StopWatchTest, DoubleStop) {
    StopWatch sw;
    sw.Start();
    auto start = std::chrono::steady_clock::now();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    auto end = std::chrono::steady_clock::now();
    auto elapsedManual = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();

    auto firstStop = sw.Stop().count();
    auto secondStop = sw.Stop().count();
    EXPECT_EQ(firstStop, secondStop); // Stopping twice should yield the same result
    EXPECT_NEAR(firstStop, elapsedManual, elapsedManual * 0.2); // Validate first stop against manual calculation
}


