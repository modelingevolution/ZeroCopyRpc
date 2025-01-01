#include "ProcessUtils.h"

#ifdef WIN32

pid_t getCurrentProcessId() {
    return GetCurrentProcessId();
}


bool is_process_running(pid_t pid) {
    HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (process == nullptr) {
        return false; // Process does not exist
    }

    DWORD exitCode;
    bool running = GetExitCodeProcess(process, &exitCode) && exitCode == STILL_ACTIVE;
    CloseHandle(process);
    return running;
}
#if defined(__aarch64__)
inline uint64_t get_cycles(void) {
    uint64_t cycles;
#if defined(__aarch64__)
    asm volatile("mrs %0, cntvct_el0" : "=r" (cycles));
#endif
    return cycles;
}
#endif
void spinWait(uint64_t cycles)
{
#if defined(__aarch64__)
    uint64_t start = get_cycles();
    while (get_cycles() - start < cycles) {
        asm volatile("isb" ::: "memory");
    }
#else
	for (uint64_t i = 0; i < cycles; ++i) {
		// Use PAUSE to reduce power consumption on x86/x64
		// This intrinsic is available for MSVC and GCC with -msse2 flag
		_mm_pause();
	}
#endif
}
#else

bool is_process_running(boost::process::pid_t pid) {
    // Send signal 0 to check if the process exists
    return (kill(pid, 0) == 0);
}

pid_t getCurrentProcessId() {
    return getpid();
}
#endif