#include "StopWatch.h"

// Default constructor initializes _storagePtr to point to _elapsedTime
StopWatch::StopWatch() noexcept : _isRunning(false), _start(), _elapsedTime(), _storagePtr(&_elapsedTime) {}

// Constructor for external storage
StopWatch::StopWatch(std::chrono::nanoseconds *store) noexcept : _isRunning(false), _start(), _elapsedTime(), _storagePtr(store) {}

StopWatch& StopWatch::Start() noexcept {
    if (!_isRunning) {
        _start = std::chrono::high_resolution_clock::now();
        _isRunning = true;
    }
    return *this;
}

StopWatch::~StopWatch() noexcept {
    Stop();
}


std::chrono::nanoseconds StopWatch::Stop() noexcept {
    if (_isRunning) {
        auto end = std::chrono::high_resolution_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(end - _start);

        // Always update _storagePtr, which could be &_elapsedTime or an external pointer
        *_storagePtr += elapsed;
        _isRunning = false;
    }
    return Total();
}

void StopWatch::Restart() noexcept {
    Stop();
    Start();
}

void StopWatch::Reset() noexcept {
    _isRunning = false;
    _start = std::chrono::high_resolution_clock::now();
    *_storagePtr = std::chrono::nanoseconds::zero();
}

double StopWatch::ElapsedMilliseconds() const noexcept {
    std::chrono::nanoseconds totalTime = _isRunning ?  std::chrono::high_resolution_clock::now()- _start + *_storagePtr : *_storagePtr;
    return std::chrono::duration<double, std::milli>(totalTime).count();
}

double StopWatch::ElapsedSeconds() const noexcept {
    std::chrono::nanoseconds totalTime = _isRunning ? std::chrono::high_resolution_clock::now() - _start + *_storagePtr : *_storagePtr;
    return std::chrono::duration<double>(totalTime).count();
}

std::chrono::nanoseconds StopWatch::Total() {
    return _isRunning ? _start - std::chrono::high_resolution_clock::now() + *_storagePtr : *_storagePtr;
}



StopWatch StopWatch::StartNew() noexcept {
    StopWatch sw;
    sw.Start();
    return sw;
}

StopWatch StopWatch::StartNew(std::chrono::nanoseconds &store) noexcept {
    StopWatch sw(&store);
    sw.Start();
    return sw;
}

// Private method for internal use
StopWatch StopWatch::StartNew(std::chrono::nanoseconds *store) noexcept {
    StopWatch sw(store);
    sw.Start();
    return sw;
}