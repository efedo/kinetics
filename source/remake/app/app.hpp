#pragma once
// Application state and the UI panels. The original menu structure (File,
// Edit, Calculate, View, Help) and its Alt-key shortcuts are kept; the
// screens become panels and tabs of one window.

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "app/platform.hpp"
#include "app/state.hpp"
#include "core/analysis.hpp"
#include "core/batch.hpp"
#include "core/import.hpp"
#include "core/plots.hpp"
#include "core/report.hpp"
#include "core/settings.hpp"

struct ImFont;

namespace classic {
class Session;
}

namespace app {

enum class Tab { Results, Graph, Residuals, StandardCurve, FileRuns, Report };

struct Fonts {
    ImFont* ui = nullptr;
    ImFont* mono = nullptr;
};

class App {
    friend class AppClassicSession;

public:
    void init(const std::filesystem::path& prefDir, const std::filesystem::path& exeDir);
    void shutdown();

    // Builds one frame of UI. Returns false when the user quits.
    bool frame();

    // Command line / test helpers.
    void open_table_file(const std::filesystem::path& path);
    void import_spreadsheet(const std::filesystem::path& path);
    void import_mpa(const std::filesystem::path& path);
    void set_model(kin::ModelId id);
    void request_fit();
    void analyse_file_runs() { analyse_all_runs(); }
    void write_current_report() { write_report(); }
    // Reports go to `path`; with batch analysis, one report per run.
    void set_report_file(const std::filesystem::path& path) {
        report_.file = path;
        report_.newFile = !std::filesystem::exists(path);
        report_.nextNumber = report_.options.startNumber;
        batch_.writeReports = true;
    }
    void show_window(const std::string& name) {
        if (name == "help") showHelp_ = true;
        else if (name == "about") openAbout_ = true;
        else if (name == "report-options") openReportOptions_ = true;
        else if (name == "fit-options") openFitOptions_ = true;
    }
    void select_tab(Tab t) { requestedTab_ = t; }
    void set_plot_type(kin::PlotType t);
    void save_settings() const;
    // True once after View > Classic interface was chosen.
    bool take_classic_request() { return std::exchange(classicRequested_, false); }

    Fonts fonts;

private:
    // ---- actions --------------------------------------------------------
    void fit_now();
    void apply_analysis(kin::AnalysisOutcome outcome, std::optional<int> runNumber);
    void mark_stale() { stale_ = true; }
    std::vector<kin::Point> table_points(int* invalidCells = nullptr) const;
    void set_table(const std::vector<kin::Point>& pts);
    void set_table_y(const std::vector<double>& ys);
    bool load_selected_run_into_table();
    void fit_selected_run();
    void analyse_all_runs();
    void show_batch_item(int index);
    void write_report();
    void write_batch_reports();
    bool append_report(const kin::Analysis& a, std::optional<int> runNumber);
    void choose_report_file();
    void copy_report();
    std::string current_report_text(bool forFile) const;
    void save_table(const std::filesystem::path& path);
    void handle_file_results();
    void paste_table_from_clipboard();
    void apply_pasted_text(const std::string& text);
    void error(const std::string& message);
    enum class Example { MichaelisMenten, Arrhenius, SpreadsheetRuns, MpaPlate };
    void open_example(Example e);
    // Fitting options: whether all are at their defaults, and a one-line summary.
    bool fit_settings_are_default() const;
    std::string fit_settings_summary() const;
    // v0 is fixed and the table has a point at zero concentration: that velocity is used.
    bool v0_measured() const;
    void load_settings();
    std::string default_dir() const;
    kin::AnalysisRequest make_request(std::vector<kin::Point> data) const;

    // ---- UI -------------------------------------------------------------
    void draw_menu_bar();
    void handle_shortcuts();
    void draw_header();
    void draw_left_panel();
    void draw_model_section();
    void draw_parameter_table();
    void draw_fit_button();
    void draw_fit_options_section();
    void draw_fit_settings();
    void draw_data_table();
    void draw_right_panel();
    void draw_getting_started();
    void draw_results_tab();
    void draw_graph_tab();
    void draw_residuals_tab();
    void draw_standard_curve_tab();
    void draw_file_runs_tab();
    void draw_report_tab();
    void draw_dialogs();
    void draw_report_options_dialog();
    void draw_help_window();
    void draw_about_dialog();

    // ---- state ----------------------------------------------------------
    std::filesystem::path prefDir_;
    std::filesystem::path exeDir_;
    kin::Settings settings_;

    kin::ModelId model_ = kin::ModelId::MichaelisMenten;
    std::vector<kin::ParamSetting> params_;
    kin::Method method_ = kin::Method::Marquardt;
    kin::GuessMode guess_ = kin::GuessMode::Automatic;
    int maxIterations_ = 0;
    bool showBounds_ = false;
    bool openFitOptions_ = false;     // expand "Fitting options" on the next frame
    float leftFooterHeight_ = 0.0f;   // Fit button and options, below the data table (last frame)

    std::vector<DataRow> rows_;
    std::string dataSource_ = "Entered by hand";
    std::optional<int> dataRun_;   // run number when the table came from a file
    std::filesystem::path tablePath_;

    std::optional<kin::Analysis> analysis_;
    std::optional<int> analysisRun_;
    bool stale_ = false;

    kin::PlotType plotType_ = kin::PlotType::Normal;
    kin::PlotOptions plotOptions_;
    bool numberPoints_ = true;
    bool refitPlot_ = true;

    ImportState import_;
    BatchState batch_;
    ReportState report_;
    std::vector<StdCurveEntry> stdCurve_;
    double stdInputY_ = 0.0;
    double stdInputX_ = 0.0;

    std::optional<Tab> requestedTab_;
    std::string errorText_;
    bool openError_ = false;
    bool openFewPoints_ = false;
    bool openReportOptions_ = false;
    bool openAbout_ = false;
    bool showHelp_ = false;
    int helpTopic_ = 0;
    enum class PendingReport { None, Current, Batch };
    PendingReport pendingReport_ = PendingReport::None;
    bool focusModel_ = false;
    bool focusTable_ = false;
    bool focusStdX_ = false;  // "new X for entered Y"
    bool focusStdY_ = false;  // "new Y for entered X"
    bool quit_ = false;
    bool classicRequested_ = false;
};

// The classic interface's view of this App's session (classic_session.cpp).
std::unique_ptr<classic::Session> make_classic_session(App& app);

}  // namespace app
