#pragma once
// Application state and the UI panels. The original menu structure (File,
// Edit, Calculate, View, Help) and its Alt-key shortcuts are kept; the
// screens become panels and tabs of one window.
//
// The window, top to bottom: the menus, a strip with the four steps of the
// work (Data, Equation, Fit, Report), a banner while a run from an imported
// file is shown, the left panel (equation, data, Fit) beside the right one
// (results and graph, standard curve, file runs, report), and a status bar
// with the next step or the latest message.

#include <array>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "app/help_text.hpp"
#include "app/platform.hpp"
#include "app/state.hpp"
#include "core/analysis.hpp"
#include "core/batch.hpp"
#include "core/import.hpp"
#include "core/plots.hpp"
#include "core/report.hpp"
#include "core/settings.hpp"

struct ImVec2;

namespace classic {
class Session;
}

namespace app {

// The Results tab holds the parameters, the graph and the residuals.
enum class Tab { Results, StandardCurve, FileRuns, Report };

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
    // help | about | report-options | fit-options | graph | residuals
    void show_window(const std::string& name);
    void select_tab(Tab t) { requestedTab_ = t; }
    void set_plot_type(kin::PlotType t);
    // View > Text size: a percentage of the normal size, clamped to kTextScales.
    static constexpr std::array<int, 6> kTextScales{80, 90, 100, 115, 130, 150};
    void set_text_scale(int percent);
    void save_settings() const;
    // True once after View > Classic interface was chosen.
    bool take_classic_request() { return std::exchange(classicRequested_, false); }
    // The classic interface works on the data table, so leave any file run
    // shown here first.
    void prepare_for_classic() { leave_run_view(); }

private:
    // ---- actions --------------------------------------------------------
    void fit_now();
    void apply_analysis(kin::AnalysisOutcome outcome, std::optional<int> runNumber);
    // A setting changed: the table's fit and the file runs' fits are out of date.
    void mark_stale() {
        stale_ = true;
        batchStale_ = true;
        notice_.reset();  // a change dismisses the message
    }
    // The table changed. The file runs depend on it only for MPA files,
    // whose concentrations come from the table.
    void mark_table_changed() {
        stale_ = true;
        if (import_.type == ImportType::Mpa) batchStale_ = true;
        notice_.reset();
    }
    std::vector<kin::Point> table_points(int* invalidCells = nullptr) const;
    void set_table(const std::vector<kin::Point>& pts);
    void set_table_y(const std::vector<double>& ys);
    void sort_table();
    bool load_selected_run_into_table();
    void fit_selected_run();
    void analyse_all_runs();
    void show_batch_item(int index);

    // While a run from the file analysis is shown, both panels show that run
    // and the table's own fit waits in tableResult_.
    bool showing_run() const {
        return tableResult_.has_value() && batch_.selected >= 0 &&
               batch_.selected < static_cast<int>(batch_.items.size());
    }
    const kin::BatchItem* shown_item() const {
        return showing_run() ? &batch_.items[static_cast<size_t>(batch_.selected)] : nullptr;
    }
    // Back to the data table and its own fit.
    void leave_run_view();
    // Fit every run again with the current settings, keeping the run shown.
    void refit_runs();
    // Copy the run shown into the table (with Undo), keeping its fit.
    void edit_shown_run_in_table();

    void write_report();
    void write_batch_reports();
    bool append_report(const kin::Analysis& a, std::optional<int> runNumber);
    void choose_report_file();
    void copy_report();
    std::string current_report_text(bool forFile) const;
    kin::ReportContext report_context(std::optional<int> runNumber, bool firstInFile) const;
    void save_table(const std::filesystem::path& path);
    void handle_file_results();
    void paste_table_from_clipboard();
    void apply_pasted_text(const std::string& text);
    // Messages go to the status bar, which shows them until they are closed
    // or the next change; modal dialogs are kept for decisions.
    // `detail` is shown when the mouse is over the message (a file's full path).
    void error(const std::string& message, const std::string& detail = {});
    void info(const std::string& message, const std::string& detail = {});
    // Empties the table; the status bar offers Undo.
    void clear_table();
    void copy_results();
    // File > Report options (Alt+O): on the Report tab once there are
    // results, otherwise in a dialog.
    void show_report_options() {
        if (analysis_) {
            requestedTab_ = Tab::Report;
            openReportSection_ = true;
        } else {
            openReportOptions_ = true;
        }
    }
    enum class Example { MichaelisMenten, Arrhenius, SpreadsheetRuns, MpaPlate };
    void open_example(Example e);
    // Fitting options: whether all are at their defaults, and a one-line summary.
    bool fit_settings_are_default() const;
    std::string fit_settings_summary() const;
    // v0 is fixed and the table has a point at zero concentration: that velocity is used.
    bool v0_measured() const;
    // The results shown were fitted to data or settings that have changed since.
    bool results_stale() const;
    // Forget the imported file (the File runs tab's close button).
    void close_import();
    std::string report_file_tooltip() const;
    void step_text_scale(int direction);
    void load_settings();
    std::string default_dir() const;
    kin::AnalysisRequest make_request(std::vector<kin::Point> data) const;
    void open_help(HelpId id) {
        showHelp_ = true;
        helpTopic_ = static_cast<int>(id);
    }

    // Units of the X and Y columns, typed under the table's headings. Blank
    // by default; reports then keep v3.51's text.
    std::string unit_of(kin::Role role, kin::ModelId model) const;
    // "[S] (µM)", or the name alone without a unit.
    std::string x_heading(kin::ModelId model) const;
    std::string y_heading(kin::ModelId model) const;

    // ---- UI: the window frame (ui_frame.cpp) ----------------------------
    void draw_menu_bar();
    void handle_shortcuts();
    void draw_step_strip();
    void draw_run_banner();
    void draw_panels(const ImVec2& size);
    void draw_status_bar(float height);
    std::string next_step_hint() const;
    // A "(?)" at the right of the current line that opens a help topic.
    void help_link(HelpId id);

    // ---- UI: left panel (ui_data.cpp) -----------------------------------
    void draw_left_panel();
    void draw_model_section();
    void draw_parameter_table();
    void draw_fit_button();
    void draw_fit_options_section();
    void draw_fit_settings();
    void draw_data_table();
    void draw_run_points();  // the shown file run, read-only

    // ---- UI: right panel (ui_results.cpp, ui_plot.cpp, ui_batch.cpp) -----
    void draw_right_panel();
    void draw_getting_started();
    void draw_results_tab();
    void draw_results_summary(const kin::Analysis& a, bool twoColumns);
    void draw_result_notes(const kin::Analysis& a);
    void draw_parameter_values(const kin::Analysis& a);
    void draw_residuals_section(const kin::Analysis& a);
    void draw_history_section(const kin::Analysis& a);
    void draw_plot_controls();
    void draw_plot(const ImVec2& size);
    void draw_standard_curve_tab();
    void draw_file_runs_tab();
    void draw_report_tab();

    // ---- UI: dialogs (ui_dialogs.cpp) -----------------------------------
    void draw_dialogs();
    void draw_report_options_dialog();
    // The report options, on the Report tab and in the dialog.
    void draw_report_options_form();
    std::string report_options_summary() const;
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
    bool showEmptyHint_ = false;      // Fit was asked for with an empty table
    int textScale_ = 100;             // View > Text size, in percent
    float leftWidthEm_ = 26.0f;       // the left panel's width, in font heights (saved)
    bool firstFrame_ = true;

    std::vector<DataRow> rows_;
    std::string dataSource_ = "Entered by hand";
    std::optional<int> dataRun_;   // run number when the table came from a file
    std::filesystem::path tablePath_;
    std::string xUnit_, yUnit_;

    std::optional<kin::Analysis> analysis_;  // the results shown
    std::optional<int> analysisRun_;
    bool stale_ = false;       // the table's fit is out of date
    bool batchStale_ = false;  // the file runs' fits are out of date
    bool reportWritten_ = false;  // a report of the results shown was written
    struct TableResult {
        std::optional<kin::Analysis> analysis;
        std::optional<int> run;
    };
    std::optional<TableResult> tableResult_;  // the table's fit, while a file run is shown

    kin::PlotType plotType_ = kin::PlotType::Normal;
    kin::PlotOptions plotOptions_;
    bool numberPoints_ = true;
    bool refitPlot_ = true;
    bool showFittedValues_ = true;  // the fitted constants in the legend
    bool residualStrip_ = true;     // residuals in a strip under the Normal plot
    float resultsFooterHeight_ = 0.0f;  // Residuals and history headers, below the plot (last frame)
    bool scrollToPlot_ = false;
    bool openResiduals_ = false;
    int residualScrollFrames_ = 0;  // scroll to the opened residuals once the layout has settled

    ImportState import_;
    BatchState batch_;
    ReportState report_;
    std::vector<StdCurveEntry> stdCurve_;
    double stdInputY_ = 0.0;
    double stdInputX_ = 0.0;

    std::optional<Tab> requestedTab_;
    // The table, data source and results before a change that can be undone.
    struct TableSnapshot {
        std::vector<DataRow> rows;
        std::string source;
        std::optional<int> run;
        std::optional<kin::Analysis> analysis;
        std::optional<int> analysisRun;
        bool stale = false;
    };
    TableSnapshot snapshot_table() const;
    void restore_table(TableSnapshot s);
    struct Notice {
        bool isError = false;
        std::string text;
        std::string detail;  // a tooltip, e.g. the full path of a file named in the text
        std::optional<TableSnapshot> undo;
        std::string undoTooltip;
    };
    std::optional<Notice> notice_;
    bool openFewPoints_ = false;
    bool openReportSection_ = false;  // expand the Report tab's options on the next frame
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

// The equation's name for the screen, with any fixed parameters:
// "Michaelis-Menten equation (Vmax fixed)". Reports keep v3.51's
// kin::model_description().
std::string plain_model_label(kin::ModelId model, const std::vector<kin::ParamSetting>& params);

// A short name for the equation, without v3.51's menu letter: "Hill (h > 0)"
// (ui_data.cpp, with the equation list).
const char* equation_name(kin::ModelId id);

// A plain judgement of a fit for the screen (ui_results.cpp): whether the
// constants are well determined, and what would help if not. Bad and Caution
// lines come first; a fit with neither gets one Good line. The reports keep
// v3.51's text.
enum class VerdictLevel { Good, Caution, Bad };
struct VerdictLine {
    VerdictLevel level = VerdictLevel::Good;
    std::string headline;
    std::string advice;  // may be empty
};
std::vector<VerdictLine> fit_verdict(const kin::Analysis& a);
VerdictLevel worst_level(const std::vector<VerdictLine>& lines);

}  // namespace app
