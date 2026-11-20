#include "classic/file/fs_util.hpp"

#include <algorithm>
#include <cctype>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace classic::fsu {
namespace fs = std::filesystem;
namespace {

bool valid_utf8(const std::string& s) {
    for (size_t i = 0; i < s.size();) {
        const auto b = static_cast<unsigned char>(s[i]);
        size_t len = 0;
        if (b < 0x80) len = 1;
        else if ((b & 0xE0) == 0xC0) len = 2;
        else if ((b & 0xF0) == 0xE0) len = 3;
        else if ((b & 0xF8) == 0xF0) len = 4;
        else return false;
        if (i + len > s.size()) return false;
        for (size_t k = 1; k < len; ++k)
            if ((static_cast<unsigned char>(s[i + k]) & 0xC0) != 0x80) return false;
        i += len;
    }
    return true;
}

bool less_no_case(const std::string& a, const std::string& b) {
    return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(), [](char x, char y) {
        return std::tolower(static_cast<unsigned char>(x)) < std::tolower(static_cast<unsigned char>(y));
    });
}

}  // namespace

std::string to_utf8(const fs::path& p) {
    try {
        const std::u8string u = p.u8string();
        return std::string(u.begin(), u.end());
    } catch (...) {
        return "?";
    }
}

fs::path from_utf8(const std::string& s) {
    try {
        if (valid_utf8(s)) return fs::path(std::u8string(s.begin(), s.end()));
        return fs::path(s);
    } catch (...) {
        return fs::path();
    }
}

std::string dir_string(const fs::path& p) {
    std::error_code ec;
    fs::path a = p.empty() ? fs::current_path(ec) : fs::absolute(p, ec);
    if (ec) a = p;
    a = a.lexically_normal();
    a.make_preferred();
    std::string s = to_utf8(a);
    while (!s.empty() && (s.back() == '/' || s.back() == '\\')) s.pop_back();
    return s;
}

std::vector<char> logical_drives() {
    std::vector<char> out;
#ifdef _WIN32
    const DWORD mask = GetLogicalDrives();
    for (int i = 0; i < 26; ++i)
        if (mask & (1u << i)) out.push_back(static_cast<char>('A' + i));
#endif
    return out;
}

bool is_hidden(const fs::path& p) {
#ifdef _WIN32
    const DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)) != 0;
#else
    (void)p;
    return false;
#endif
}

std::vector<std::string> list_names(const fs::path& dir, bool folders) {
    std::vector<std::string> out;
    std::error_code ec;
    fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec);
    for (; !ec && it != fs::directory_iterator(); it.increment(ec)) {
        std::error_code ec2;
        if (it->is_directory(ec2) != folders) continue;
        if (is_hidden(it->path())) continue;
        out.push_back(to_utf8(it->path().filename()));
    }
    std::sort(out.begin(), out.end(), less_no_case);
    return out;
}

bool exists(const fs::path& p) {
    std::error_code ec;
    return !p.empty() && fs::exists(p, ec);
}

bool is_directory(const fs::path& p) {
    std::error_code ec;
    return !p.empty() && fs::is_directory(p, ec);
}

bool is_empty_or_missing(const fs::path& p) {
    std::error_code ec;
    if (!fs::exists(p, ec)) return true;
    const auto size = fs::file_size(p, ec);
    return !ec && size == 0;
}

}  // namespace classic::fsu
