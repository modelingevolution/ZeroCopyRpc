#include "CrossPlatform.h"

#ifdef WIN32

#else

error_t strncpy_s(char* dest, const char* src, size_t destsz)
{
    if (dest == NULL || src == NULL) {
        return -1;  // EINVAL: Invalid argument
    }

    if (destsz == 0) {
        return -1;  // EINVAL: Invalid argument
    }

    // Get the length of the source string
    size_t len = strlen(src);

    // If the source string is longer than the destination buffer, copy only up to destsz-1
    size_t copy_len = len < destsz - 1 ? len : destsz - 1;

    // Copy the source string into the destination buffer
    memcpy(dest, src, copy_len);

    // Null-terminate the destination buffer
    dest[copy_len] = '\0';

    return 0;  // Success
}
#endif