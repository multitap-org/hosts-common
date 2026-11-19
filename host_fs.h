#pragma once

// Small file helpers for hosts (no SDL). Paths are UTF-8.
// Every failure is logged (host_log) with the path, so callers just return.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Our paths use '/' on every platform (Windows takes it too). Paths that come
// from Windows itself (the program's folder, the home folder, a dropped file)
// use '\\'; this turns them into '/' so a path never mixes both. Does nothing
// on other platforms, where '\\' is a valid file name character.
void host_fs_forward_slashes(char* path);

// True if `path` exists (file or directory).
bool host_fs_exists(const char* path);

// True if `path` is a directory (or a symlink to one).
bool host_fs_is_directory(const char* path);

// Creates `path` and any missing parent directories ("mkdir -p").
// Succeeds if the directory already exists.
bool host_fs_make_directories(const char* path);

// Reads a whole file into a new[] buffer the caller owns (delete[]).
// An empty file succeeds with *size == 0 (and a 1-byte buffer).
bool host_fs_read_file(const char* path, uint8_t** data, size_t* size);

// Writes `size` bytes so that `path` is never left half-written: the data
// goes to "<path>.tmp" first, which then replaces `path`. Creates missing
// parent directories.
bool host_fs_write_file(const char* path, const void* data, size_t size);

// The same, readable and writable only by the user (0600 on POSIX): for
// secrets, like a device's private key.
bool host_fs_write_private_file(const char* path, const void* data, size_t size);

// Copies a file, or a directory with everything inside it, to `to`.
// Files that already exist at the destination are kept, not overwritten.
// Creates missing parent directories.
bool host_fs_copy(const char* from, const char* to);

// "dir/name.ext" -> "name" (no directory, no last extension). Returns false
// if it doesn't fit in `out`.
bool host_fs_stem(const char* path, char* out, size_t out_size);

// The real absolute path of an existing file or folder: "." and ".." gone,
// every symlink followed, '/' separators. False (logged at debug level: a
// missing path is often just a wrong guess) if it doesn't exist or doesn't
// fit in `out`.
bool host_fs_canonical(const char* path, char* out, size_t out_size);

// Is the existing `path` the folder `folder` or inside it, once both are
// canonical (host_fs_canonical)? "/roms/../etc" and a symlink inside
// "/roms" pointing out of it are both outside. For letting a remote device
// reach only some folders: anything that doesn't resolve is outside.
bool host_fs_is_inside(const char* folder, const char* path);

// One entry of a folder (host_fs_list_directory).
struct HostFsEntry
{
    std::string name;             // UTF-8, no folder part
    bool        is_directory = false;   // after following a symlink
    bool        is_symlink = false;     // the entry itself is a link
    uint64_t    size = 0;               // files only
};

// What's in the folder `path`, in no particular order ("." and ".." left
// out; entries whose type can't be read are skipped). False (logged) if it
// can't be read.
bool host_fs_list_directory(const char* path, std::vector<HostFsEntry>* entries);
