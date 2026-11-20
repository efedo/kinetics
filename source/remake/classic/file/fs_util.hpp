#pragma once
// File-system helpers for the file browser (DIRECT.CPP) and KIN_FILE.CPP.
// v3.51 used DOS calls (findfirst, getdisk, access); these are the modern
// equivalents. Nothing here throws: a coroutine that throws terminates.

#include <filesystem>
#include <string>
#include <vector>

namespace classic::fsu {

// The separator the browser shows and builds paths with.
#ifdef _WIN32
inline constexpr char kSep = '\\';
#else
inline constexpr char kSep = '/';
#endif

// Paths as UTF-8 strings (put_str converts them to code page 437), and back.
// A string that is not valid UTF-8 is read in the native narrow encoding.
std::string to_utf8(const std::filesystem::path& p);
std::filesystem::path from_utf8(const std::string& s);

// A folder in the browser's form: absolute, native separators, and no
// trailing separator, so that a drive root is "C:" and the POSIX root is "".
std::string dir_string(const std::filesystem::path& p);

// The drive letters in use (Windows); empty elsewhere.
std::vector<char> logical_drives();

// Hidden or system files and folders, which findfirst() did not return.
bool is_hidden(const std::filesystem::path& p);

// The names (UTF-8, sorted without regard to case) of the folders or of the
// other entries in `dir`, skipping hidden and system ones.
std::vector<std::string> list_names(const std::filesystem::path& dir, bool folders);

bool exists(const std::filesystem::path& p);
bool is_directory(const std::filesystem::path& p);
bool is_empty_or_missing(const std::filesystem::path& p);

}  // namespace classic::fsu
