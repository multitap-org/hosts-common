#include "dynamic_library.h"

#include <cstring>

#include "host_log.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif

// ---------------------------------------------------------------------------
// Linked libraries (every platform)
// ---------------------------------------------------------------------------

struct LinkedEntry
{
    LinkedLibrary library    = {};
    int32_t       open_count = 0;   // opens not closed yet (dynamic_library_is_loaded)
};

static LinkedEntry g_linked[DYNAMIC_LIBRARY_LINKED_MAX] = {};
static int32_t     g_linked_count                       = 0;

void dynamic_library_set_linked(const LinkedLibrary* libraries, int32_t count)
{
    g_linked_count = 0;
    for (int32_t i = 0; i < count; i++)
    {
        if (g_linked_count == DYNAMIC_LIBRARY_LINKED_MAX)
        {
            host_log(LogLevel::error, "Too many linked libraries: %s ignored", libraries[i].file_name);
            continue;
        }
        g_linked[g_linked_count] = LinkedEntry{};
        g_linked[g_linked_count].library = libraries[i];
        g_linked_count++;
    }
}

// The file name part of a path (after the last slash or backslash).
static const char* file_name_of(const char* path)
{
    const char* name = path;
    for (const char* c = path; *c != '\0'; c++)
    {
        if (*c == '/' || *c == '\\')
        {
            name = c + 1;
        }
    }
    return name;
}

// The linked library `path` names, or nullptr.
static LinkedEntry* find_linked(const char* path)
{
    const char* name = file_name_of(path);
    for (int32_t i = 0; i < g_linked_count; i++)
    {
        if (std::strcmp(g_linked[i].library.file_name, name) == 0)
        {
            return &g_linked[i];
        }
    }
    return nullptr;
}

static GenericFunction linked_symbol(const LinkedEntry* entry, const char* name)
{
    for (int32_t i = 0; i < entry->library.symbol_count; i++)
    {
        if (std::strcmp(entry->library.symbols[i].name, name) == 0)
        {
            return entry->library.symbols[i].function;
        }
    }
    return nullptr;
}

// The OS's loader, below (one per platform).
static bool            os_open(DynamicLibrary* library, const char* path);
static GenericFunction os_symbol(const DynamicLibrary* library, const char* name);
static void            os_close(DynamicLibrary* library);
static bool            os_is_loaded(const char* path);

bool dynamic_library_open(DynamicLibrary* library, const char* path)
{
    LinkedEntry* entry = find_linked(path);
    if (entry != nullptr)
    {
        entry->open_count++;
        library->handle = entry;
        library->linked = true;
        return true;
    }
    library->linked = false;
    return os_open(library, path);
}

GenericFunction dynamic_library_symbol(const DynamicLibrary* library, const char* name)
{
    if (library->handle == nullptr)
    {
        return nullptr;
    }
    if (library->linked)
    {
        return linked_symbol(static_cast<const LinkedEntry*>(library->handle), name);
    }
    return os_symbol(library, name);
}

void dynamic_library_close(DynamicLibrary* library)
{
    if (library->handle == nullptr)
    {
        return;
    }
    if (library->linked)
    {
        static_cast<LinkedEntry*>(library->handle)->open_count--;
        library->handle = nullptr;
        library->linked = false;
        return;
    }
    os_close(library);
}

bool dynamic_library_is_loaded(const char* path)
{
    const LinkedEntry* entry = find_linked(path);
    if (entry != nullptr)
    {
        return entry->open_count > 0;
    }
    return os_is_loaded(path);
}

#if defined(_WIN32)

// ---------------------------------------------------------------------------
// Windows
// ---------------------------------------------------------------------------

static bool os_open(DynamicLibrary* library, const char* path)
{
    // Paths are UTF-8 everywhere in our code; Windows wants UTF-16 for non-ASCII paths.
    int wide_length = MultiByteToWideChar(CP_UTF8, 0, path, -1, nullptr, 0);
    if (wide_length <= 0)
    {
        host_log(LogLevel::error, "Invalid UTF-8 in library path: %s", path);
        return false;
    }

    wchar_t* wide_path = new wchar_t[static_cast<size_t>(wide_length)];
    MultiByteToWideChar(CP_UTF8, 0, path, -1, wide_path, wide_length);

    HMODULE module = LoadLibraryW(wide_path);
    delete[] wide_path;

    if (module == nullptr)
    {
        host_log(LogLevel::error, "LoadLibraryW failed for %s (error %lu)", path, GetLastError());
        return false;
    }

    library->handle = reinterpret_cast<void*>(module);
    return true;
}

static GenericFunction os_symbol(const DynamicLibrary* library, const char* name)
{
    FARPROC address = GetProcAddress(reinterpret_cast<HMODULE>(library->handle), name);
    return reinterpret_cast<GenericFunction>(address);
}

static void os_close(DynamicLibrary* library)
{
    FreeLibrary(reinterpret_cast<HMODULE>(library->handle));
    library->handle = nullptr;
}

static bool os_is_loaded(const char* path)
{
    int wide_length = MultiByteToWideChar(CP_UTF8, 0, path, -1, nullptr, 0);
    if (wide_length <= 0)
    {
        return false;
    }
    wchar_t* wide_path = new wchar_t[static_cast<size_t>(wide_length)];
    MultiByteToWideChar(CP_UTF8, 0, path, -1, wide_path, wide_length);
    // GetModuleHandleW doesn't load anything or change the reference count.
    HMODULE module = GetModuleHandleW(wide_path);
    delete[] wide_path;
    return module != nullptr;
}

#else

// ---------------------------------------------------------------------------
// POSIX (Linux, macOS, Android)
// ---------------------------------------------------------------------------

static bool os_open(DynamicLibrary* library, const char* path)
{
    // RTLD_LOCAL: the core's symbols don't leak into the global namespace,
    // which matters once two different cores are loaded in one process.
    // RTLD_NOW: fail here on missing dependencies, not later in the middle of a frame.
    void* handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (handle == nullptr)
    {
        const char* reason = dlerror();
        host_log(LogLevel::error, "dlopen failed: %s", reason != nullptr ? reason : "unknown error");
        return false;
    }

    library->handle = handle;
    return true;
}

static GenericFunction os_symbol(const DynamicLibrary* library, const char* name)
{
    // POSIX guarantees dlsym results can be converted to function pointers.
    void* address = dlsym(library->handle, name);
    return reinterpret_cast<GenericFunction>(address);
}

static void os_close(DynamicLibrary* library)
{
    dlclose(library->handle);
    library->handle = nullptr;
}

static bool os_is_loaded(const char* path)
{
    // RTLD_NOLOAD: only succeeds if it's already loaded (the loader matches
    // the file itself, so other spellings of the path are caught). It still
    // returns a new reference, which we give back right away.
    void* handle = dlopen(path, RTLD_LAZY | RTLD_NOLOAD);
    if (handle == nullptr)
    {
        dlerror();   // clear the "not loaded" error so it doesn't show up later
        return false;
    }
    dlclose(handle);
    return true;
}

#endif
