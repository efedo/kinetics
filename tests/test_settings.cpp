// Tests for core/settings: the "key = value" preferences file.

#include <climits>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>

#include "core/settings.hpp"

#include "check.hpp"

namespace {

using kin::Settings;

// Scratch folder relative to the working directory (the build folder under
// ctest); each test removes it again.
const std::filesystem::path kTmp = std::filesystem::path("test_tmp") / "settings_c";

void fresh_tmp() {
    std::error_code ec;
    std::filesystem::remove_all(kTmp, ec);
    std::filesystem::create_directories(kTmp, ec);
}

void clean_tmp() {
    std::error_code ec;
    std::filesystem::remove_all(kTmp, ec);
    // Remove test_tmp too if nothing else is in it (remove_all, because a
    // plain remove of a directory fails under Node.js on Windows).
    if (std::filesystem::is_empty(kTmp.parent_path(), ec) && !ec) std::filesystem::remove_all(kTmp.parent_path(), ec);
}

// Text mode on both sides, so line ends compare as '\n' on every platform.
std::string read_text(const std::filesystem::path& p) {
    std::ifstream in(p);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

void write_bytes(const std::filesystem::path& p, const std::string& s) {
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out << s;
}

}  // namespace

TEST_CASE(settings_round_trip, "settings: save writes sorted 'key = value' lines that load reads back") {
    fresh_tmp();
    Settings s;
    s.set("theme", "light");
    s.set_int("report.format", 2);
    s.set_bool("autoscale", true);
    s.set("header", "Kinetics of PFK");
    s.set_bool("grid", false);
    s.set_int("start", -3);
    s.set_int("min", INT_MIN);
    const auto file = kTmp / "prefs.ini";
    CHECK(s.save(file));
    CHECK(read_text(file) ==
          "# Kinetics preferences\n"
          "autoscale = 1\n"
          "grid = 0\n"
          "header = Kinetics of PFK\n"
          "min = -2147483648\n"
          "report.format = 2\n"
          "start = -3\n"
          "theme = light\n");

    Settings t;
    CHECK(t.load(file));
    CHECK(t.get("theme") == "light");
    CHECK(t.get("header") == "Kinetics of PFK");
    CHECK(t.get_int("report.format", 0) == 2);
    CHECK(t.get_int("start", 0) == -3);
    CHECK(t.get_int("min", 0) == INT_MIN);
    CHECK(t.get_bool("autoscale", false));
    CHECK(!t.get_bool("grid", true));
    // Saving the loaded copy gives the same file.
    const auto copy = kTmp / "copy.ini";
    CHECK(t.save(copy));
    CHECK(read_text(copy) == read_text(file));
    clean_tmp();
}

TEST_CASE(settings_load_syntax, "settings: load skips comments, blanks and lines without '=', and trims") {
    fresh_tmp();
    const auto file = kTmp / "hand.ini";
    write_bytes(file,
                "# comment = not a value\r\n"
                "\r\n"
                "   \t \r\n"
                "  key one  =  value with spaces  \r\n"
                "noequals\r\n"
                "expr = a=b\r\n"
                "empty =\r\n"
                " = novalue\r\n"
                "\tdup = 1\r\n"
                "dup = 2\r\n"
                "  # indented comment = x\r\n"
                "last=no newline");
    Settings s;
    CHECK(s.load(file));
    CHECK(s.get("key one") == "value with spaces");
    CHECK(s.get("expr") == "a=b");            // split at the first '='
    CHECK(s.get("empty", "fallback").empty());  // present, but empty
    CHECK(s.get("") == "novalue");            // an empty key is still a key
    CHECK(s.get("dup") == "2");               // the later line wins
    CHECK(s.get("last") == "no newline");
    CHECK(s.get("noequals", "fallback") == "fallback");
    CHECK(s.get("# comment", "fallback") == "fallback");
    CHECK(s.get("# indented comment", "fallback") == "fallback");
    CHECK(s.get("missing") .empty());
    clean_tmp();
}

TEST_CASE(settings_typed_values, "settings: integers and booleans, with their fallbacks") {
    Settings s;
    s.set("i1", "42");
    s.set("i2", "-7");
    s.set("i3", "abc");
    s.set("i4", "");
    s.set("i5", "99999999999");  // out of range for int
    s.set("i6", " 5");           // from_chars does not skip spaces
    CHECK(s.get_int("i1", 0) == 42);
    CHECK(s.get_int("i2", 0) == -7);
    CHECK(s.get_int("i3", 13) == 13);
    CHECK(s.get_int("i4", 13) == 13);
    CHECK(s.get_int("i5", 13) == 13);
    CHECK(s.get_int("i6", 13) == 13);
    CHECK(s.get_int("missing", 13) == 13);

    for (const char* yes : {"1", "true", "yes"}) {
        s.set("b", yes);
        CHECK(s.get_bool("b", false));
    }
    // Anything else reads as false, not as the fallback.
    for (const char* no : {"0", "false", "no", "TRUE", "maybe", ""}) {
        s.set("b", no);
        CHECK(!s.get_bool("b", true));
    }
    CHECK(s.get_bool("missing", true));
    CHECK(!s.get_bool("missing", false));

    s.set_bool("flag", true);
    CHECK(s.get("flag") == "1");
    s.set_bool("flag", false);
    CHECK(s.get("flag") == "0");
    s.set_int("n", 2026);
    CHECK(s.get("n") == "2026");
    s.set("n", "later");
    CHECK(s.get("n") == "later" && s.get_int("n", -1) == -1);
}

TEST_CASE(settings_merge, "settings: load adds to and overrides the values already set") {
    fresh_tmp();
    const auto file = kTmp / "merge.ini";
    write_bytes(file, "dup = new\nadded = 3\n");
    Settings s;
    s.set("keep", "yes");
    s.set("dup", "old");
    CHECK(s.load(file));
    CHECK(s.get("keep") == "yes");
    CHECK(s.get("dup") == "new");
    CHECK(s.get_int("added", 0) == 3);
    clean_tmp();
}

TEST_CASE(settings_failures, "settings: missing files and unwritable paths") {
    fresh_tmp();
    Settings s;
    s.set("a", "1");
    CHECK(!s.load(kTmp / "missing.ini"));
    CHECK(s.get("a") == "1");  // unchanged
    CHECK(!s.save(kTmp / "no_such_folder" / "x.ini"));
    clean_tmp();
}
