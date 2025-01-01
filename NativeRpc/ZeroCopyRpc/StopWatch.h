//
// Created by pi on 29/11/24.
//

#ifndef STOPWATCH_H
#define STOPWATCH_H

#include <chrono>
#include <optional>
#include <stdexcept>
#include <Export.h>

class EXPORT StopWatch {
public:
    using clock = std::chrono::steady_clock;
    using time_point = typename clock::time_point;
    using duration = typename clock::duration;

    // Default constructor does not start the stopwatch
    StopWatch() noexcept;

    // Should stop the stopwach and store the duration if this is the last reference.
    ~StopWatch() noexcept;

    // Start the stopwatch
    StopWatch& Start() noexcept;

    // Stop the stopwatch and return elapsed time in milliseconds
    std::chrono::nanoseconds Stop() noexcept;
    void Restart() noexcept ;
    // Reset the stopwatch
    void Reset() noexcept;


    // Get elapsed time in milliseconds without stopping
    double ElapsedMilliseconds() const noexcept;

    // Get elapsed time in seconds without stopping
    double ElapsedSeconds() const noexcept ;

    std::chrono::nanoseconds Total();

    // Static method to start a new StopWatch
    static StopWatch StartNew() noexcept;
    static StopWatch StartNew(std::chrono::nanoseconds &store) noexcept;

private:
    // A ctor that would store duration with the pointer.
    StopWatch(std::chrono::nanoseconds *store) noexcept;

    static StopWatch StartNew(std::chrono::nanoseconds *store) noexcept;
    time_point _start;
    std::chrono::nanoseconds _elapsedTime;
    std::chrono::nanoseconds* _storagePtr;;
    bool _isRunning;

};




#endif //STOPWATCH_H
