#pragma once

#include <stddef.h>  // For size_t
#include <string.h>  // For memcpy

#ifdef WIN32

#else
int strncpy_s(char* dest, const char* src, size_t destsz);
#endif