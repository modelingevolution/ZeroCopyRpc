#pragma once
#include <chrono>
#include <thread>
#include "ThreadSpin.h"
#include "Export.h"

class EXPORT PeriodicTimer {
public:
    explicit PeriodicTimer(std::chrono::nanoseconds period);
    bool WaitForNext();
    static PeriodicTimer CreateFromFrequency(int Hz);

private:
    const std::chrono::nanoseconds _period;
    std::chrono::steady_clock::time_point _nxTick;
    ThreadSpin _spin;

    bool WaitUntil(std::chrono::steady_clock::time_point target);
};