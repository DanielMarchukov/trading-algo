#pragma once

#include <cstring>

#ifdef _WIN32
#define strncpy(dest, src, count) strncpy_s(dest, src, count)
#define setenv(name, value, overwrite) _putenv_s(name, value)
#define unsetenv(name) _putenv_s(name, "")
#endif
