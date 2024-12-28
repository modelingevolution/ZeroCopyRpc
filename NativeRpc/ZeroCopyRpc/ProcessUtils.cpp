#include "ProcessUtils.h"

#ifdef WIN32
#include <windows.h>
pid_t getCurrentProcessId() {
    return GetCurrentProcessId();
}
#else
#include <unistd.h>
pid_t getCurrentProcessId() {
    return getpid();
}
#endif