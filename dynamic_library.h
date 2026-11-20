#pragma once

// Loading shared libraries (.so / .dll / .dylib) and looking up functions.
// Thin wrapper over dlopen/dlsym (POSIX) and LoadLibraryW/GetProcAddress (Windows).
//
// Linked libraries: a program can also carry a "library" linked into itself
// (the browser build links its core statically: no dlopen there). It
// registers it once with dynamic_library_set_linked(); from then on opening
// any path whose file name matches opens that one, and its symbols come from
// the table. The rest of the code (host-libretro, the core catalog) doesn't
// know the difference.

#include <cstdint>

// A function pointer of "unknown" type. Callers cast it to the real signature
// with reinterpret_cast (allowed by STYLE.md for loader function pointers).
// Casting a function pointer to another function pointer type and back is
// well-defined, which is why we use this instead of void*.
typedef void (*GenericFunction)(void);

struct DynamicLibrary
{
    void* handle = nullptr;   // dlopen handle, HMODULE, or a linked library's entry
    bool  linked = false;     // handle is a linked library (not the OS's)
};

// One function of a linked library.
struct LinkedSymbol
{
    const char*     name     = nullptr;
    GenericFunction function = nullptr;
};

// A library linked into the program: the file name it answers to (no folder,
// e.g. "sameboy_libretro.so") and its functions. The arrays must outlive
// every use (static tables).
struct LinkedLibrary
{
    const char*         file_name    = nullptr;
    const LinkedSymbol* symbols      = nullptr;
    int32_t             symbol_count = 0;
};

constexpr int32_t DYNAMIC_LIBRARY_LINKED_MAX = 8;

// Registers the program's linked libraries (replaces any earlier list; at
// most DYNAMIC_LIBRARY_LINKED_MAX, the rest are ignored with a log line).
// Call it before anything opens a library.
void dynamic_library_set_linked(const LinkedLibrary* libraries, int32_t count);

// path: UTF-8. A linked library whose file name matches path's is opened
// instead of the file. Logs the OS error on failure.
bool dynamic_library_open(DynamicLibrary* library, const char* path);

// Returns nullptr if the symbol isn't exported (does not log; callers decide).
GenericFunction dynamic_library_symbol(const DynamicLibrary* library, const char* name);

// Safe on a library that was never opened.
void dynamic_library_close(DynamicLibrary* library);

// True if the library at `path` (UTF-8) is already loaded in this process
// (a linked library: opened and not closed yet),
// however the path is spelled (symlink, "dir//lib.so"...): the OS identifies
// it by the file, not the string. Never loads it and leaves its reference
// count as it was.
bool dynamic_library_is_loaded(const char* path);
