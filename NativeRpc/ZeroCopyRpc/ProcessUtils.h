#pragma once
#ifdef WIN32
#include <windows.h>
typedef DWORD pid_t;
#else
#include <sys/types.h>
#endif

pid_t getCurrentProcessId();
