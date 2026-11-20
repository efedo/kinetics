#include "core/settings.hpp"

#include <charconv>
#include <fstream>

namespace kin {
namespace {

std::string trim(const std::string& s) {
    const auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return {};
    const auto e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

}  // namespace

bool Settings::load(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in) return false;
    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        values_[trim(line.substr(0, eq))] = trim(line.substr(eq + 1));
    }
    return true;
}

bool Settings::save(const std::filesystem::path& path) const {
    std::ofstream out(path, std::ios::trunc);
    if (!out) return false;
    out << "# Kinetics preferences\n";
    for (const auto& [k, v] : values_) out << k << " = " << v << "\n";
    return static_cast<bool>(out);
}

std::string Settings::get(const std::string& key, const std::string& fallback) const {
    const auto it = values_.find(key);
    return it == values_.end() ? fallback : it->second;
}

int Settings::get_int(const std::string& key, int fallback) const {
    const auto it = values_.find(key);
    if (it == values_.end()) return fallback;
    int v = fallback;
    const auto& s = it->second;
    const auto res = std::from_chars(s.data(), s.data() + s.size(), v);
    return res.ec == std::errc() ? v : fallback;
}

bool Settings::get_bool(const std::string& key, bool fallback) const {
    const auto it = values_.find(key);
    if (it == values_.end()) return fallback;
    return it->second == "1" || it->second == "true" || it->second == "yes";
}

void Settings::set(const std::string& key, const std::string& value) { values_[key] = value; }
void Settings::set_int(const std::string& key, int value) { values_[key] = std::to_string(value); }
void Settings::set_bool(const std::string& key, bool value) { values_[key] = value ? "1" : "0"; }

}  // namespace kin
