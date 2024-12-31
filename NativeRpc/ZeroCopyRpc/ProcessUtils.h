#pragma once
#include "Export.h"
#ifdef WIN32
#include <windows.h>
typedef DWORD pid_t;
#else
#include <sys/types.h>
#include <signal.h>
#include <unistd.h>
#endif

pid_t EXPORT getCurrentProcessId();
bool EXPORT is_process_running(pid_t pid);
#include <intrin.h> // For _mm_pause
#include <atomic>

void EXPORT spinWait(int cycles);
