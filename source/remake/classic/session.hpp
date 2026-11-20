#pragma once
// What the classic interface shares with the modern one. The modern App
// implements this (app/classic_session.cpp), so both interfaces work on the
// same equation, parameter settings, data, imported file, results and report
// settings, and switching between them shows the same work.

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "app/state.hpp"
#include "core/analysis.hpp"

namespace classic {

class Session {
public:
    virtual ~Session() = default;

    // ---- equation and fitting settings ---------------------------------------
    virtual kin::ModelId model() const = 0;
    // Changes the equation and resets its parameter settings to the defaults.
    virtual void set_model(kin::ModelId id) = 0;
    // Fixed flags, fixed values / manual estimates and bounds, in the
    // equation's parameter order (the same order as v3.51's p[1..4]).
    virtual std::vector<kin::ParamSetting>& params() = 0;
    virtual kin::Method& method() = 0;
    virtual kin::GuessMode& guess() = 0;
    // Call after changing settings or data: marks the results out of date.
    virtual void changed() = 0;

    // ---- data table -------------------------------------------------------------
    // The rows that hold two numbers, in table order.
    virtual std::vector<kin::Point> table() const = 0;
    // Replaces the table. `source` is shown in the modern interface's header
    // ("Entered by hand", "Run #3 of file.txt", ...); `run` is the imported
    // file's run or dataset number when the data came from one.
    virtual void set_table(const std::vector<kin::Point>& pts, const std::string& source,
                           std::optional<int> run = std::nullopt) = 0;
    // Where the table's data came from, as set_table() last recorded it.
    virtual std::string table_source() const = 0;
    virtual std::optional<int> table_run() const = 0;

    // ---- results ------------------------------------------------------------------
    // A request for `data` with the current equation and settings.
    virtual kin::AnalysisRequest request(std::vector<kin::Point> data) const = 0;
    // Stores a result so that both interfaces show it. `run` is the imported
    // file's run or dataset number, if the data came from one.
    virtual void set_result(kin::Analysis a, std::optional<int> run) = 0;
    virtual const kin::Analysis* result() const = 0;
    virtual std::optional<int> result_run() const = 0;
    virtual bool result_stale() const = 0;

    // ---- imported file, batch results, reports -------------------------------------
    virtual app::ImportState& import_state() = 0;
    virtual app::BatchState& batch_state() = 0;
    virtual app::ReportState& report_state() = 0;
    // Import a file; on failure returns the error message and leaves the
    // previous import in place.
    virtual std::optional<std::string> import_spreadsheet(const std::filesystem::path& path) = 0;
    virtual std::optional<std::string> import_mpa(const std::filesystem::path& path) = 0;
    // Writes or appends text to a file. In the browser the whole file is
    // then downloaded, as the modern interface does with reports.
    virtual bool write_text(const std::filesystem::path& path, const std::string& text, bool append) = 0;
    // Folder to start file browsing in (last used, or the examples).
    virtual std::filesystem::path default_dir() const = 0;
    virtual void save_settings() = 0;
    virtual bool is_web() const = 0;

    // Browser only: ask for a file from the user's computer with the
    // browser's file picker; the file is copied into the page's file system.
    // Poll take_uploaded_file(), with `co_await next_frame()` between polls.
    // There is no notice when the user cancels the picker.
    virtual void request_upload() = 0;
    virtual std::optional<std::filesystem::path> take_uploaded_file() = 0;
};

}  // namespace classic
