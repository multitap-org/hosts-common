#pragma once

// Logging for host code. Writes to stderr.
//
// Hosts don't link SDL (they will also run as separate processes later),
// so they can't use SDL_Log. This is the host-side equivalent.

#include <cstdarg>
#include <cstdio>

// printf-style format checking for our own variadic functions. On MinGW plain
// `printf` means the old msvcrt printf (no %zu); mingw's stdio.h names the one
// that matches the C runtime actually used (UCRT understands %zu).
#if defined(__MINGW32__)
#define HOST_PRINTF_FORMAT(format_index, first_arg) __attribute__((format(__MINGW_PRINTF_FORMAT, format_index, first_arg)))
#elif defined(__GNUC__)
#define HOST_PRINTF_FORMAT(format_index, first_arg) __attribute__((format(printf, format_index, first_arg)))
#else
#define HOST_PRINTF_FORMAT(format_index, first_arg)
#endif

enum class LogLevel
{
    debug,
    info,
    warning,
    error,
};

// Messages below this level are dropped. Default: LogLevel::info.
void host_log_set_min_level(LogLevel level);

// printf-style. A newline is added if `format` doesn't end with one.
void host_log(LogLevel level, const char* format, ...) HOST_PRINTF_FORMAT(2, 3);

// Same as host_log(), for callers that already have a va_list
// (e.g. the libretro log callback). `prefix` may be nullptr.
void host_log_va(LogLevel level, const char* prefix, const char* format, va_list args);
