#pragma once
#include "Export.h"
#ifdef WIN32
#include <WinSock2.h> 
#include <windows.h>
typedef DWORD pid_t;
#else
#include <sys/types.h>
#include <signal.h>
#include <unistd.h>
#endif

pid_t EXPORT getCurrentProcessId();
bool EXPORT is_process_running(pid_t pid);

#if defined(__aarch64__)
#include <stdint.h>

#else
#include <intrin.h> // For _mm_pause
#include <atomic>
#endif


