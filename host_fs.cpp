#include "host_fs.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <system_error>

#include "host_log.h"

// The only file that uses <filesystem> (STYLE.md). Every call takes a
// std::error_code: those overloads report errors there and never throw
// (we build with -fno-exceptions).

// Our paths are UTF-8. A path built from plain char text uses the system's
// narrow encoding, which isn't UTF-8 on Windows; char8_t text is always read
// as UTF-8 (C++20). Same bytes, so the cast through void* is exact.
static std::filesystem::path utf8_path(const char* path)
{
    const char8_t* text = static_cast<const char8_t*>(static_cast<const void*>(path));
    return std::filesystem::path(text);
}

// A file time as seconds since 1970 (0 before it).
static uint64_t seconds_since_1970(std::filesystem::file_time_type time)
{
    std::chrono::system_clock::time_point system = std::chrono::file_clock::to_sys(time);
    int64_t seconds = std::chrono::duration_cast<std::chrono::seconds>(system.time_since_epoch()).count();
    return seconds > 0 ? static_cast<uint64_t>(seconds) : 0;
}

// fopen with a UTF-8 path: Windows needs the UTF-16 one (_wfopen).
static FILE* open_file(const std::filesystem::path* path, bool for_writing)
{
#if defined(_WIN32)
    return _wfopen(path->c_str(), for_writing ? L"wb" : L"rb");
#else
    return std::fopen(path->c_str(), for_writing ? "wb" : "rb");
#endif
}

// Windows also accepts '\\' as a separator.
static bool is_separator(char c)
{
#if defined(_WIN32)
    return c == '/' || c == '\\';
#else
    return c == '/';
#endif
}

void host_fs_forward_slashes(char* path)
{
#if defined(_WIN32)
    for (char* cursor = path; *cursor != '\0'; cursor++)
    {
        if (*cursor == '\\')
        {
            *cursor = '/';
        }
    }
#else
    (void)path;
#endif
}

bool host_fs_exists(const char* path)
{
    std::error_code error;
    return std::filesystem::exists(utf8_path(path), error);
}

bool host_fs_is_directory(const char* path)
{
    std::error_code error;
    return std::filesystem::is_directory(utf8_path(path), error);
}

bool host_fs_make_directories(const char* path)
{
    if (path[0] == '\0')
    {
        host_log(LogLevel::error, "Empty directory path");
        return false;
    }
    std::filesystem::path directory = utf8_path(path);
    std::error_code error;
    // Returns false without an error when it already exists.
    std::filesystem::create_directories(directory, error);
    if (error)
    {
        host_log(LogLevel::error, "Could not create directory %s: %s", path, error.message().c_str());
        return false;
    }
    if (!std::filesystem::is_directory(directory, error))
    {
        host_log(LogLevel::error, "Not a directory: %s", path);
        return false;
    }
    return true;
}

bool host_fs_read_file(const char* path, uint8_t** data, size_t* size)
{
    *data = nullptr;
    *size = 0;

    std::filesystem::path file_path = utf8_path(path);
    std::error_code error;
    uintmax_t file_size = std::filesystem::file_size(file_path, error);
    if (error)
    {
        host_log(LogLevel::error, "Could not get the size of %s: %s", path, error.message().c_str());
        return false;
    }
    if (file_size > SIZE_MAX)
    {
        host_log(LogLevel::error, "File too big: %s", path);
        return false;
    }

    FILE* file = open_file(&file_path, false);
    if (file == nullptr)
    {
        host_log(LogLevel::error, "Could not open %s", path);
        return false;
    }

    size_t length = static_cast<size_t>(file_size);
    // new[] of size 0 is valid, but a 1-byte buffer keeps "data != nullptr" true.
    uint8_t* buffer = new uint8_t[length > 0 ? length : 1];
    size_t read = std::fread(buffer, 1, length, file);
    std::fclose(file);

    if (read != length)
    {
        host_log(LogLevel::error, "Short read on %s (%zu of %zu bytes)", path, read, length);
        delete[] buffer;
        return false;
    }

    *data = buffer;
    *size = length;
    return true;
}

static bool write_file(const char* path, const void* data, size_t size, bool owner_only)
{
    std::filesystem::path file_path = utf8_path(path);
    std::error_code error;

    std::filesystem::path directory = file_path.parent_path();
    if (!directory.empty())
    {
        std::filesystem::create_directories(directory, error);
        if (error)
        {
            host_log(LogLevel::error, "Could not create the directory for %s: %s", path, error.message().c_str());
            return false;
        }
    }

    std::filesystem::path temporary = file_path;
    temporary += ".tmp";

    FILE* file = open_file(&temporary, true);
    if (file == nullptr)
    {
        host_log(LogLevel::error, "Could not create %s.tmp", path);
        return false;
    }
    if (owner_only)
    {
        // Before anything is written, so the data is never readable by others.
        // On Windows this only touches the read-only flag (a user's profile
        // folder is private there already).
        std::filesystem::permissions(temporary, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
                                     std::filesystem::perm_options::replace, error);
        if (error)
        {
            host_log(LogLevel::error, "Could not make %s.tmp private: %s", path, error.message().c_str());
            std::fclose(file);
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
            return false;
        }
    }
    bool ok = size == 0 || std::fwrite(data, 1, size, file) == size;
    ok = std::fflush(file) == 0 && ok;
    ok = std::fclose(file) == 0 && ok;
    if (!ok)
    {
        host_log(LogLevel::error, "Could not write %s.tmp", path);
        std::filesystem::remove(temporary, error);
        return false;
    }

    // Replaces an existing file in one step, on Windows too (MoveFileExW).
    std::filesystem::rename(temporary, file_path, error);
    if (error)
    {
        host_log(LogLevel::error, "Could not replace %s: %s", path, error.message().c_str());
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return false;
    }
    return true;
}

bool host_fs_write_file(const char* path, const void* data, size_t size)
{
    return write_file(path, data, size, false);
}

bool host_fs_write_private_file(const char* path, const void* data, size_t size)
{
    return write_file(path, data, size, true);
}

bool host_fs_copy(const char* from, const char* to)
{
    std::filesystem::path source = utf8_path(from);
    std::filesystem::path destination = utf8_path(to);
    std::error_code error;

    std::filesystem::path parent = destination.parent_path();
    if (!parent.empty())
    {
        std::filesystem::create_directories(parent, error);
        if (error)
        {
            host_log(LogLevel::error, "Could not create the directory for %s: %s", to, error.message().c_str());
            return false;
        }
    }
    std::filesystem::copy(source, destination,
                          std::filesystem::copy_options::recursive | std::filesystem::copy_options::skip_existing,
                          error);
    if (error)
    {
        host_log(LogLevel::error, "Could not copy %s to %s: %s", from, to, error.message().c_str());
        return false;
    }
    return true;
}

bool host_fs_stem(const char* path, char* out, size_t out_size)
{
    const char* name = path;
    for (const char* cursor = path; *cursor != '\0'; cursor++)
    {
        if (is_separator(*cursor))
        {
            name = cursor + 1;
        }
    }
    const char* dot = std::strrchr(name, '.');
    size_t length = dot != nullptr && dot != name ? static_cast<size_t>(dot - name) : std::strlen(name);
    if (length + 1 > out_size)
    {
        return false;
    }
    std::memcpy(out, name, length);
    out[length] = '\0';
    return true;
}

bool host_fs_canonical(const char* path, char* out, size_t out_size)
{
    if (path[0] == '\0')
    {
        return false;
    }
    std::error_code error;
    std::filesystem::path canonical = std::filesystem::canonical(utf8_path(path), error);
    if (error)
    {
        host_log(LogLevel::debug, "No such path %s: %s", path, error.message().c_str());
        return false;
    }
    // generic_u8string: '/' separators and UTF-8 on every platform.
    std::u8string text = canonical.generic_u8string();
    if (text.size() + 1 > out_size)
    {
        host_log(LogLevel::error, "Path too long: %s", path);
        return false;
    }
    std::memcpy(out, text.data(), text.size());
    out[text.size()] = '\0';
    return true;
}

bool host_fs_is_inside(const char* folder, const char* path)
{
    // Heap buffers: two 4 KiB paths are too much for some threads' stacks.
    constexpr size_t PATH_SIZE = 4096;
    std::string canonical_folder(PATH_SIZE, '\0');
    std::string canonical_path(PATH_SIZE, '\0');
    if (!host_fs_canonical(folder, &canonical_folder[0], PATH_SIZE) ||
        !host_fs_canonical(path, &canonical_path[0], PATH_SIZE))
    {
        return false;
    }
    size_t folder_length = std::strlen(canonical_folder.c_str());
    if (std::strncmp(canonical_path.c_str(), canonical_folder.c_str(), folder_length) != 0)
    {
        return false;
    }
    // "/roms" holds "/roms" and "/roms/x", not "/roms2". A root ("/",
    // "C:/") already ends in '/'.
    char next = canonical_path[folder_length];
    return next == '\0' || next == '/' || canonical_folder[folder_length - 1] == '/';
}

bool host_fs_list_directory(const char* path, std::vector<HostFsEntry>* entries)
{
    entries->clear();
    std::error_code error;
    std::filesystem::directory_iterator iterator(utf8_path(path), error);
    if (error)
    {
        host_log(LogLevel::error, "Could not list %s: %s", path, error.message().c_str());
        return false;
    }
    // <filesystem> lists folders only through an iterator (STYLE.md allows
    // it here). Walked by hand with increment(error): a range-for would use
    // the throwing increment.
    std::filesystem::directory_iterator end;
    while (iterator != end)
    {
        std::filesystem::directory_entry item = *iterator;   // a copy: no reference kept
        std::error_code type_error;
        bool directory = item.is_directory(type_error);   // follows symlinks
        bool file = !type_error && item.is_regular_file(type_error);
        if (!type_error && (directory || file))
        {
            HostFsEntry entry;
            std::u8string name = item.path().filename().u8string();
            entry.name.assign(static_cast<const char*>(static_cast<const void*>(name.data())), name.size());
            entry.is_directory = directory;
            std::error_code link_error;
            entry.is_symlink = item.is_symlink(link_error);
            if (file)
            {
                std::error_code size_error;
                uint64_t size = item.file_size(size_error);
                entry.size = size_error ? 0 : size;
                std::error_code time_error;
                std::filesystem::file_time_type time = item.last_write_time(time_error);
                entry.mtime = time_error ? 0 : seconds_since_1970(time);
            }
            entries->push_back(entry);
        }
        iterator.increment(error);
        if (error)
        {
            host_log(LogLevel::error, "Could not list %s: %s", path, error.message().c_str());
            return false;
        }
    }
    return true;
}

bool host_fs_file_info(const char* path, uint64_t* size, uint64_t* mtime)
{
    std::filesystem::path file_path = utf8_path(path);
    std::error_code error;
    if (!std::filesystem::is_regular_file(file_path, error) || error)
    {
        host_log(LogLevel::debug, "%s is not a file", path);
        return false;
    }
    uint64_t file_size = std::filesystem::file_size(file_path, error);
    if (error)
    {
        host_log(LogLevel::error, "Could not read the size of %s: %s", path, error.message().c_str());
        return false;
    }
    std::filesystem::file_time_type time = std::filesystem::last_write_time(file_path, error);
    *size = file_size;
    *mtime = error ? 0 : seconds_since_1970(time);
    return true;
}

bool host_fs_set_mtime(const char* path, uint64_t mtime)
{
    std::chrono::system_clock::time_point system{std::chrono::seconds(static_cast<int64_t>(mtime))};
    std::filesystem::file_time_type time = std::chrono::file_clock::from_sys(system);
    std::error_code error;
    std::filesystem::last_write_time(utf8_path(path), time, error);
    if (error)
    {
        host_log(LogLevel::warning, "Could not set the date of %s: %s", path, error.message().c_str());
        return false;
    }
    return true;
}

bool host_fs_is_symlink(const char* path)
{
    std::error_code error;
    bool link = std::filesystem::is_symlink(std::filesystem::symlink_status(utf8_path(path), error));
    return !error && link;
}

bool host_fs_rename(const char* from, const char* to)
{
    std::error_code error;
    std::filesystem::rename(utf8_path(from), utf8_path(to), error);
    if (error)
    {
        host_log(LogLevel::error, "Could not move %s to %s: %s", from, to, error.message().c_str());
        return false;
    }
    return true;
}

bool host_fs_remove_file(const char* path)
{
    std::error_code error;
    std::filesystem::remove(utf8_path(path), error);
    if (error)
    {
        host_log(LogLevel::error, "Could not delete %s: %s", path, error.message().c_str());
        return false;
    }
    return true;
}

bool host_fs_free_space(const char* path, uint64_t* bytes)
{
    std::error_code error;
    std::filesystem::space_info info = std::filesystem::space(utf8_path(path), error);
    if (error)
    {
        host_log(LogLevel::error, "Could not read the free space at %s: %s", path, error.message().c_str());
        return false;
    }
    *bytes = info.available;
    return true;
}

struct HostFsFile
{
    FILE*       file = nullptr;
    std::string path;   // for the log
};

static HostFsFile* open_with_mode(const char* path, const char* mode, const wchar_t* wide_mode)
{
    std::filesystem::path file_path = utf8_path(path);
#if defined(_WIN32)
    (void)mode;
    FILE* file = _wfopen(file_path.c_str(), wide_mode);
#else
    (void)wide_mode;
    FILE* file = std::fopen(file_path.c_str(), mode);
#endif
    if (file == nullptr)
    {
        host_log(LogLevel::error, "Could not open %s", path);
        return nullptr;
    }
    HostFsFile* handle = new HostFsFile;
    handle->file = file;
    handle->path = path;
    return handle;
}

HostFsFile* host_fs_open_read(const char* path, uint64_t offset)
{
    HostFsFile* file = open_with_mode(path, "rb", L"rb");
    if (file == nullptr)
    {
        return nullptr;
    }
#if defined(_WIN32)
    int failed = _fseeki64(file->file, static_cast<__int64>(offset), SEEK_SET);
#else
    int failed = fseeko(file->file, static_cast<off_t>(offset), SEEK_SET);
#endif
    if (failed != 0)
    {
        host_log(LogLevel::error, "Could not seek in %s", path);
        std::fclose(file->file);
        delete file;
        return nullptr;
    }
    return file;
}

HostFsFile* host_fs_open_append(const char* path)
{
    return open_with_mode(path, "ab", L"ab");
}

bool host_fs_file_read(HostFsFile* file, uint8_t* data, size_t size, size_t* read)
{
    *read = std::fread(data, 1, size, file->file);
    if (*read < size && std::ferror(file->file) != 0)
    {
        host_log(LogLevel::error, "Could not read %s", file->path.c_str());
        return false;
    }
    return true;
}

bool host_fs_file_write(HostFsFile* file, const uint8_t* data, size_t size)
{
    if (std::fwrite(data, 1, size, file->file) != size)
    {
        host_log(LogLevel::error, "Could not write %s", file->path.c_str());
        return false;
    }
    return true;
}

bool host_fs_file_close(HostFsFile* file)
{
    if (file == nullptr)
    {
        return true;
    }
    bool ok = std::fclose(file->file) == 0;
    if (!ok)
    {
        host_log(LogLevel::error, "Could not finish writing %s", file->path.c_str());
    }
    delete file;
    return ok;
}
