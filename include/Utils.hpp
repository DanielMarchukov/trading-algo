#pragma once

#include <string.h>

#ifdef _WIN32
    #define strncpy(dest, src, count) strncpy_s(dest, src, count)
#else
    #define strncpy(dest, src, count) strncpy(dest, src, count)
#endif