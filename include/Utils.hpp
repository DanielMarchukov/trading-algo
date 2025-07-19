#pragma once

#include <string.h>

#ifdef _WIN32
    #define strncpy(dest, src, count) strncpy_s(dest, src, count)
#endif