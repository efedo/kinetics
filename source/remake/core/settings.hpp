#pragma once
// Persistent preferences in a small "key = value" text file. Replaces the
// binary OUTPUT.DAT / INPUT.DAT records of v3.51, which stored raw FILE*
// pointers and were read in text mode.

#include <filesystem>
#include <map>
#include <string>

namespace kin {

class Settings {
public:
    bool load(const std::filesystem::path& path);
    bool save(const std::filesystem::path& path) const;

    std::string get(const std::string& key, const std::string& fallback = {}) const;
    int get_int(const std::string& key, int fallback) const;
    bool get_bool(const std::string& key, bool fallback) const;

    void set(const std::string& key, const std::string& value);
    void set_int(const std::string& key, int value);
    void set_bool(const std::string& key, bool value);

private:
    std::map<std::string, std::string> values_;
};

}  // namespace kin
