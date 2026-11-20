// The App's implementation of classic::Session: the classic interface reads
// and changes the same session as the modern one.

#include <cmath>

#include "app/app.hpp"
#include "app/platform.hpp"
#include "classic/session.hpp"

namespace app {

class AppClassicSession final : public classic::Session {
public:
    explicit AppClassicSession(App& app) : a_(app) {}

    kin::ModelId model() const override { return a_.model_; }
    void set_model(kin::ModelId id) override { a_.set_model(id); }
    std::vector<kin::ParamSetting>& params() override { return a_.params_; }
    kin::Method& method() override { return a_.method_; }
    kin::GuessMode& guess() override { return a_.guess_; }
    void changed() override { a_.mark_stale(); }

    std::vector<kin::Point> table() const override {
        std::vector<kin::Point> out;
        for (const auto& p : a_.table_points())
            if (std::isfinite(p.x) && std::isfinite(p.y)) out.push_back(p);
        return out;
    }

    void set_table(const std::vector<kin::Point>& pts, const std::string& source, std::optional<int> run) override {
        a_.set_table(pts);
        a_.dataSource_ = source;
        a_.dataRun_ = run;
    }

    std::string table_source() const override { return a_.dataSource_; }
    std::optional<int> table_run() const override { return a_.dataRun_; }

    kin::AnalysisRequest request(std::vector<kin::Point> data) const override { return a_.make_request(std::move(data)); }

    void set_result(kin::Analysis analysis, std::optional<int> run) override {
        kin::AnalysisOutcome outcome;
        outcome.analysis = std::move(analysis);
        a_.apply_analysis(std::move(outcome), run);
        a_.batch_.selected = -1;
    }

    const kin::Analysis* result() const override { return a_.analysis_ ? &*a_.analysis_ : nullptr; }
    std::optional<int> result_run() const override { return a_.analysisRun_; }
    bool result_stale() const override { return a_.stale_; }

    ImportState& import_state() override { return a_.import_; }
    BatchState& batch_state() override { return a_.batch_; }
    ReportState& report_state() override { return a_.report_; }

    std::optional<std::string> import_spreadsheet(const std::filesystem::path& path) override {
        a_.import_spreadsheet(path);
        return take_error();
    }

    std::optional<std::string> import_mpa(const std::filesystem::path& path) override {
        a_.import_mpa(path);
        return take_error();
    }

    bool write_text(const std::filesystem::path& path, const std::string& text, bool append) override {
        if (!kin::write_text_file(path, text, append)) return false;
        file_written(path);
        return true;
    }

    std::filesystem::path default_dir() const override { return a_.default_dir(); }
    void save_settings() override { a_.save_settings(); }
    bool is_web() const override { return kIsWeb; }

    void request_upload() override {
        uploaded_.reset();
        show_open_dialog(FilePurpose::ImportSpreadsheet, a_.default_dir());
    }

    std::optional<std::filesystem::path> take_uploaded_file() override {
        for (const auto& r : take_file_dialog_results())
            if (!r.path.empty()) uploaded_ = path_from_utf8_string(r.path);
        return std::exchange(uploaded_, std::nullopt);
    }

private:
    // App reports errors through a modal; the classic interface shows its
    // own, so take the message and close the modal before it is drawn.
    std::optional<std::string> take_error() {
        if (!a_.openError_) return std::nullopt;
        a_.openError_ = false;
        return std::exchange(a_.errorText_, std::string());
    }

    static std::filesystem::path path_from_utf8_string(const std::string& s) {
        return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(s.data()), s.size()));
    }

    App& a_;
    std::optional<std::filesystem::path> uploaded_;
};

std::unique_ptr<classic::Session> make_classic_session(App& app) { return std::make_unique<AppClassicSession>(app); }

}  // namespace app
