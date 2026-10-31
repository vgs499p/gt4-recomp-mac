#pragma once
// macOS / POSIX port: the MSVC "secure" CRT calls and the few plain Win32 queries used by the runtime and its
// fixtures, mapped onto POSIX equivalents with the Windows return conventions. On Windows this header is empty.

#ifndef _WIN32

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <unistd.h>

inline int fopen_s(std::FILE **file, const char *path, const char *mode) {
  *file = std::fopen(path, mode);
  return *file ? 0 : errno;
}

inline int tmpfile_s(std::FILE **file) {
  *file = std::tmpfile();
  return *file ? 0 : errno;
}

// _putenv_s with an empty value removes the variable, as on Windows.
inline int _putenv_s(const char *name, const char *value) {
  const int result = (value && *value) ? setenv(name, value, 1) : unsetenv(name);
  return result == 0 ? 0 : errno;
}

// GetEnvironmentVariableA: the length copied (without the terminator), the size needed when the buffer is too small,
// or 0 when the variable is unset.
using DWORD = unsigned int;
inline DWORD GetEnvironmentVariableA(const char *name, char *buffer, DWORD size) {
  const char *value = std::getenv(name);
  if (!value) return 0;
  const std::size_t length = std::strlen(value);
  if (length + 1 > size) return static_cast<DWORD>(length + 1);
  std::memcpy(buffer, value, length + 1);
  return static_cast<DWORD>(length);
}

inline DWORD GetCurrentProcessId() { return static_cast<DWORD>(getpid()); }

// Milliseconds since an arbitrary fixed point (monotonic), as GetTickCount64.
inline unsigned long long GetTickCount64() {
  timespec now{};
  clock_gettime(CLOCK_MONOTONIC, &now);
  return static_cast<unsigned long long>(now.tv_sec) * 1000u + static_cast<unsigned long long>(now.tv_nsec) / 1000000u;
}

#endif
