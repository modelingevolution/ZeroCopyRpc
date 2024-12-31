#pragma once
#include <atomic>
#include <chrono>
#include <export.h>

class EXPORT ThreadSpin {
public:
    ThreadSpin();

    static void Wait(int cycles);

    template<typename Duration>
    void Wait(Duration duration) {
        auto target = std::chrono::steady_clock::now() + duration;

        while (std::chrono::high_resolution_clock::now() < target) {
            auto remaining = target - std::chrono::high_resolution_clock::now();
            auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(remaining).count();
            int cycles = static_cast<int>(ns / ns_per_cycle * 0.9);

            if (cycles > 0) {
                OnWait(cycles);
            }
        }
    }

private:
    std::atomic<double> ns_per_cycle;

    void OnWait(int cycles);

    static double Calibrate();
};