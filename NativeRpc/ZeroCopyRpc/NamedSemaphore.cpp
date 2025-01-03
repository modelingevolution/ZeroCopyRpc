#include "NamedSemaphore.h"

NamedSemaphore::NamedSemaphore(const std::string& name, OpenMode mode, unsigned int initialCount) : _name( FormatName(name))
{
#ifdef _WIN32
	auto lp_name = _name.c_str();
    switch (mode) {
    case OpenMode::Create:
        _handle = CreateSemaphoreA(
            nullptr,                // default security attributes
            initialCount,           // initial count
            0x7FFFFFFF,            // maximum count
            lp_name          // semaphore name
        );
        if (_handle != nullptr && GetLastError() == ERROR_ALREADY_EXISTS) {
            CloseHandle(_handle);
            throw std::system_error(ERROR_ALREADY_EXISTS, std::system_category(), "Semaphore already exists");
        }
        break;

    case OpenMode::Open:
        _handle = OpenSemaphoreA(
            SEMAPHORE_ALL_ACCESS,   // desired access
            FALSE,                  // inherit handle
            lp_name           // semaphore name
        );

        break;

    case OpenMode::OpenOrCreate:
        _handle = CreateSemaphoreA(
            nullptr,                // default security attributes
            initialCount,           // initial count
            0x7FFFFFFF,            // maximum count
            lp_name          // semaphore name
        );
        break;
    }

    if (_handle == nullptr) {
        throw std::system_error(GetLastError(), std::system_category(), "Failed to create/open semaphore");
    }
#else
    switch (mode) {
    case OpenMode::Create:
        _handle = sem_open(_name.c_str(), O_CREAT | O_EXCL, 0644, initialCount);
        break;

    case OpenMode::Open:
        _handle = sem_open(_name.c_str(), 0);
        break;

    case OpenMode::OpenOrCreate:
        _handle = sem_open(_name.c_str(), O_CREAT, 0644, initialCount);
        break;
    }

    if (_handle == SEM_FAILED) {
        throw std::system_error(errno, std::system_category(), "Failed to create/open semaphore");
    }
#endif
}

NamedSemaphore::NamedSemaphore(const std::string& name, unsigned int initialCount): NamedSemaphore(name, OpenMode::OpenOrCreate, initialCount)
{
}

NamedSemaphore::~NamedSemaphore()
{
	if (IsValid()) {
#ifdef _WIN32
		CloseHandle(_handle);
#else
            sem_close(_handle);
            sem_unlink(_name.c_str());
#endif
	}
}

NamedSemaphore::NamedSemaphore(NamedSemaphore&& other) noexcept: _handle(other._handle), _name(std::move(other._name))
{
	other._handle = nullptr;
}

NamedSemaphore& NamedSemaphore::operator=(NamedSemaphore&& other) noexcept
{
	if (this != &other) {
		if (IsValid()) {
#ifdef _WIN32
			CloseHandle(_handle);
#else
                sem_close(_handle);
#endif
		}
		_handle = other._handle;
		_name = std::move(other._name);
		other._handle = nullptr;
	}
	return *this;
}

bool NamedSemaphore::Remove(const std::string& name)
{
#ifdef _WIN32
	std::string formattedName = FormatName(name);
	HANDLE semaphore = OpenSemaphoreA(DELETE, FALSE, formattedName.c_str());
	if (semaphore == nullptr) {
		// If we can't open it, it might not exist, which is fine for removal
		return GetLastError() == ERROR_FILE_NOT_FOUND;
	}

	// On Windows, closing the last handle automatically removes the semaphore
	bool result = CloseHandle(semaphore);
	return result;
#else
        std::string formattedName = FormatName(name);
        return sem_unlink(formattedName.c_str()) == 0 || errno == ENOENT;
#endif
}

void NamedSemaphore::Acquire()
{
#ifdef _WIN32
	if (WaitForSingleObject(_handle, INFINITE) == WAIT_FAILED) {
		throw std::system_error(GetLastError(), std::system_category(), "Failed to acquire semaphore");
	}
#else
        if (sem_wait(_handle) != 0) {
            throw std::system_error(errno, std::system_category(), "Failed to acquire semaphore");
        }
#endif
}
bool NamedSemaphore::TryAcquireFor(const std::chrono::milliseconds& timeout) {
#ifdef _WIN32
    // Convert timeout to milliseconds for Windows API
    DWORD result = WaitForSingleObject(_handle, static_cast<DWORD>(timeout.count()));

    if (result == WAIT_FAILED) {
        throw std::system_error(GetLastError(), std::system_category(), "Failed to try acquire semaphore with timeout");
    }
    return result == WAIT_OBJECT_0;

#else
    // Use system clock for calculating absolute timeout
    auto now = std::chrono::system_clock::now();
    auto absTimeout = now + timeout;

    // Convert to timespec for POSIX semaphores
    timespec ts;
    ts.tv_sec = std::chrono::duration_cast<std::chrono::seconds>(absTimeout.time_since_epoch()).count();
    ts.tv_nsec = std::chrono::duration_cast<std::chrono::nanoseconds>(absTimeout.time_since_epoch() % std::chrono::seconds(1)).count();

    // Call POSIX sem_timedwait
    int result = sem_timedwait(_handle, &ts);
    if (result == 0) {
        return true;  // Successfully acquired
    }

    if (errno == ETIMEDOUT) {
        return false;  // Timeout occurred
    }

    // Handle other errors
    throw std::system_error(errno, std::system_category(), "Failed to try acquire semaphore with timeout");
#endif
}

bool NamedSemaphore::TryAcquire()
{
#ifdef _WIN32
	DWORD result = WaitForSingleObject(_handle, 0);
	if (result == WAIT_FAILED) {
		throw std::system_error(GetLastError(), std::system_category(), "Failed to try acquire semaphore");
	}
	return result == WAIT_OBJECT_0;
#else
        return sem_trywait(_handle) == 0;
#endif
}

void NamedSemaphore::Release(unsigned int count)
{
#ifdef _WIN32
	if (!ReleaseSemaphore(_handle, count, nullptr)) {
		throw std::system_error(GetLastError(), std::system_category(), "Failed to release semaphore");
	}
#else
        for (unsigned int i = 0; i < count; ++i) {
            if (sem_post(_handle) != 0) {
                throw std::system_error(errno, std::system_category(), "Failed to release semaphore");
            }
        }
#endif
}

int NamedSemaphore::GetCount() const
{
#ifdef _WIN32
	LONG previousCount;
	if (!ReleaseSemaphore(_handle, 0, &previousCount)) {
		throw std::system_error(GetLastError(), std::system_category(), "Failed to get semaphore count");
	}
	return previousCount;
#else
        int value;
        if (sem_getvalue(_handle, &value) != 0) {
            throw std::system_error(errno, std::system_category(), "Failed to get semaphore count");
        }
        return value;
#endif
}

bool NamedSemaphore::IsValid() const
{
#ifdef _WIN32
	return _handle != nullptr;
#else
        return _handle != nullptr && _handle != SEM_FAILED;
#endif
}

std::string NamedSemaphore::FormatName(const std::string& name)
{
#ifdef _WIN32
	// Windows semaphore names must be prefixed with "Global\"
	return "Global\\" + name;
#else
        // POSIX semaphore names must start with "/"
        return "/" + name;
#endif
}
