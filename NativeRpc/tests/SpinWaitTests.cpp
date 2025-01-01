#include <gtest/gtest.h>
#include <chrono>
#include <iostream>
#include "ProcessUtils.h" // Include the header where spinWait is declared
#include "ThreadSpin.h"

TEST(SpinWaitTest, MeasureCycleDuration)
{
    const int total_duration_ms = 1000; // Test will run for 1 second
    const int min_cycles_per_test = 100; // Minimum cycles to spin for per iteration
    const int cycles_per_test = 1000;    // For the 1000 cycles test

    std::chrono::high_resolution_clock::time_point start, end;
    int iterations_min_cycles = 0;
    int iterations_thousand_cycles = 0;

    // Measure 100 cycles over 1 second
    start = std::chrono::high_resolution_clock::now();
    while (true) {
        ThreadSpin::Wait(min_cycles_per_test);  // 100 cycles
        iterations_min_cycles++;

        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - start).count();
        if (elapsed >= total_duration_ms) {
            break;
        }
    }
    end = std::chrono::high_resolution_clock::now();

    // Calculate average time for 100 cycles, then for one cycle
    double total_time_min_cycles_ms = std::chrono::duration<double, std::micro>(end - start).count();
    double avg_time_min_cycles_ms = total_time_min_cycles_ms / iterations_min_cycles;
    double avg_time_one_cycle_ms = avg_time_min_cycles_ms / min_cycles_per_test;
    std::cout << "Average time for one cycle: " << avg_time_one_cycle_ms << " µs" << std::endl;
    std::cout << "Average time for 100 cycle: " << 100*avg_time_one_cycle_ms << " µs" << std::endl;

    // Reset for 1000 cycles test
    start = std::chrono::high_resolution_clock::now();
    iterations_thousand_cycles = 0;

    // Measure 1000 cycles over 1 second
    while (true) {
        ThreadSpin::Wait(cycles_per_test);  // 1000 cycles
        iterations_thousand_cycles++;

        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - start).count();
        if (elapsed >= total_duration_ms) {
            break;
        }
    }
    end = std::chrono::high_resolution_clock::now();

    // Calculate average time for 1000 cycles
    double total_time_thousand_cycles_ms = std::chrono::duration<double, std::milli>(end - start).count();
    double avg_time_thousand_cycles_ms = total_time_thousand_cycles_ms / iterations_thousand_cycles;
    std::cout << "Average time for " << cycles_per_test << " cycles: " << avg_time_thousand_cycles_ms << " ms" << std::endl;
}