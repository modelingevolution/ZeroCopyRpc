#pragma once
#ifdef WIN32
#include <windows.h>
typedef DWORD pid_t;
#else
#include <sys/types.h>
#include <signal.h>
#include <unistd.h>
#endif

pid_t getCurrentProcessId();
bool is_process_running(pid_t pid);