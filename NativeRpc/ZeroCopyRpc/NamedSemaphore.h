#pragma once

#include <string>
#include <system_error>
#include <chrono>
#include "Export.h"

#ifdef _WIN32
#include <WinSock2.h> 
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <semaphore.h>
#include <errno.h>
#endif

class EXPORT NamedSemaphore {
public:
    enum class OpenMode {
        Create,        // Create new, fail if exists
        Open,         // Open existing, fail if doesn't exist
        OpenOrCreate  // Open if exists, create if doesn't
    };

    NamedSemaphore() = delete;

    // Creates or opens a named semaphore with initial count
    NamedSemaphore(const std::string& name, OpenMode mode, unsigned int initialCount = 0);
    NamedSemaphore(const std::string& name, unsigned int initialCount = 0);

    ~NamedSemaphore();

    // Deleted copy constructor and assignment operator
    NamedSemaphore(const NamedSemaphore&) = delete;
    NamedSemaphore& operator=(const NamedSemaphore&) = delete;

    // Move constructor
    NamedSemaphore(NamedSemaphore&& other) noexcept;

    // Move assignment operator
    NamedSemaphore& operator=(NamedSemaphore&& other) noexcept;

    // Removes a named semaphore from the system
    static bool Remove(const std::string& name);

    // Acquires the semaphore (decrements count)
    void Acquire();

    // Tries to acquire the semaphore, returns true if successful
    bool TryAcquire();

    // Tries to acquire the semaphore with timeout
    
    bool TryAcquireFor(const std::chrono::milliseconds& timeout);

    // Releases the semaphore (increments count)
    void Release(unsigned int count = 1);

    // Gets current semaphore count (if supported by platform)
    int GetCount() const;

private:
#ifdef _WIN32
    HANDLE _handle = nullptr;
#else
    sem_t* _handle = nullptr;
#endif
    std::string _name;

    bool IsValid() const;

    static std::string FormatName(const std::string& name);
};

