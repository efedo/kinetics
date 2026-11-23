#pragma once
// Application state and the UI panels. The original menu structure (File,
// Edit, Calculate, View, Help) and its Alt-key shortcuts are kept; the
// screens become panels and tabs of one window.
//
// The window, top to bottom: the menus, a strip with the five steps of the
// work (Data, Equation, Fit, Results, Report), the left panel (the run
// shown, then its Data, the Equation and the Fitting options) beside the
// right one (Runs, Results and Report tabs), and a status bar naming the
// data, or with the latest message.
//
// An analysis is a list of runs (state.hpp), all fitted with the same
// equation and options. It can be saved to a file and opened again
// (core/project.hpp).

#include <array>
#include <filesystem>
#include <functional>
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
enum class Tab { Runs, Results, Report };

class App {
    friend class AppClassicSession;

public:
    void init(const std::filesystem::path& prefDir, const std::filesystem::path& exeDir);
    void shutdown();

    // Builds one frame of UI. Returns false when the user quits.
    bool frame();

    // Command line / test helpers.
    // A two-column data table into the run shown (Undo in the status bar).
    void open_table_file(const std::filesystem::path& path);
    // A spreadsheet file of runs, or a saved analysis, in place of every run:
    // with `ask`, once the user agrees to lose the data there is.
    void import_runs_file(const std::filesystem::path& path, bool ask = true,
                          std::optional<kin::ModelId> model = std::nullopt);
    void open_analysis_file(const std::filesystem::path& path, bool ask = true);
    // The classic interface's file imports (v3.51's file of runs or MPA file).
    void import_spreadsheet(const std::filesystem::path& path);
    void import_mpa(const std::filesystem::path& path);
    void set_model(kin::ModelId id);
    void request_fit();
    void fit_all_runs();
    void write_current_report() { write_report(); }
    // Reports go to `path`: Write report adds the report of every fitted run.
    void set_report_file(const std::filesystem::path& path) {
        report_.file = path;
        report_.newFile = !std::filesystem::exists(path);
        report_.nextNumber = report_.options.startNumber;
    }
    // help | about | report-options | fit-options | graph | residuals
    void show_window(const std::string& name);
    void select_tab(Tab t) { requestedTab_ = t; }
    // The Calculate box's inputs: a Y to read X for, and an X to read Y for.
    void set_predictions(const std::string& forY, const std::string& forX) {
        predictFromY_ = forY;
        predictFromX_ = forX;
    }
    void set_plot_type(kin::PlotType t);
    // View > Text size: a percentage of the normal size, clamped to kTextScales.
    static constexpr std::array<int, 6> kTextScales{80, 90, 100, 115, 130, 150};
    void set_text_scale(int percent);
    void save_settings() const;
    // True once after View > Classic interface was chosen.
    bool take_classic_request() { return std::exchange(classicRequested_, false); }
    // The classic interface works on the data table: the run shown here.
    void prepare_for_classic() { commit_run(); }

private:
    // ---- actions --------------------------------------------------------
    void fit_now();
    void apply_analysis(kin::AnalysisOutcome outcome, std::optional<int> runNumber);
    // A setting changed: every run's fit is out of date.
    void mark_stale() {
        stale_ = true;
        for (auto& r : runs_)
            if (r.analysis) r.stale = true;
        notice_.reset();  // a change dismisses the message
    }
    // The table changed. Its fit no longer describes it, so the results go
    // at once, and the report and predictions made from them with it. The
    // Data section opens to show the table.
    void mark_table_changed() {
        analysis_.reset();
        analysisRun_.reset();
        stale_ = false;
        runs_[static_cast<size_t>(current_)].error.clear();
        reportWritten_ = false;
        refitPlot_ = true;
        runsAsImported_ = false;  // the file can no longer be read again in its place
        notice_.reset();
        dataOpen_ = true;
    }
    std::vector<kin::Point> table_points(int* invalidCells = nullptr) const;
    void set_table(const std::vector<kin::Point>& pts);
    void sort_table();

    // ---- runs -------------------------------------------------------------
    // The run shown, runs_[current_], is worked on in rows_, analysis_,
    // stale_ and dataSource_; its entry in runs_ is brought up to date by
    // commit_run(), which everything that reads all the runs calls first.
    void commit_run();
    void load_run(int k);
    void select_run(int k);
    void add_run();
    void delete_run(int k);
    void fit_run(int k);
    // "Run 2", or "Run 2: WT" when the run has a name of its own.
    std::string run_label(int k) const;
    // A run's name unless it only repeats its number ("2", "Run 2").
    std::string run_own_name(int k) const;
    int run_point_count(int k) const;
    const kin::Analysis* run_analysis(int k) const;
    bool run_stale(int k) const;
    bool any_results() const;
    // Whether any run holds a number (replacing them all then needs a yes).
    bool have_data() const;
    // The run number a report gives: the run's when there are several.
    std::optional<int> report_run_number(int k) const;
    void replace_runs(std::vector<Run> runs);
    // Replacing every run cannot be undone, so ask first when there is
    // data; otherwise just do it.
    void confirm_replace(const std::string& what, std::function<void()> action);
    void new_analysis();
    void save_analysis(const std::filesystem::path& path);
    // File > Save analysis: to its file, or asking for one the first time.
    void save_analysis_or_ask();
    void ask_analysis_file();
    void export_runs(const std::filesystem::path& path);
    // The runs just imported, read again from the file with other options
    // (runs in rows; negative values missing), until they are edited.
    void reread_runs_file();

    void write_report();
    bool append_report(const kin::Analysis& a, std::optional<int> runNumber, bool first);
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
        if (any_results()) {
            requestedTab_ = Tab::Report;
            openReportSection_ = true;
        } else {
            openReportOptions_ = true;
        }
    }
    enum class Example { MichaelisMenten, Arrhenius, SpreadsheetRuns, RunsInColumns };
    void open_example(Example e);
    // Whether all the fit settings (Fitting options) are at their defaults.
    bool fit_settings_are_default() const;
    // v0 is fixed and the table has a point at zero concentration: that velocity is used.
    bool v0_measured() const;
    // The results shown were fitted with an equation or settings that have
    // changed since.
    bool results_stale() const { return analysis_ && stale_; }
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
    void draw_panels(const ImVec2& size);
    void draw_status_bar(float height);
    // A "(?)" at the right of the current line that opens a help topic.
    void help_link(HelpId id);

    // ---- UI: left panel (ui_data.cpp) -----------------------------------
    void draw_left_panel();
    // The run shown: its number and name, with buttons to step through the runs.
    void draw_run_heading();
    // The left panel's three sections, in the order of the steps; each folds.
    enum class Section { Data, Equation, Fit };
    // A section's heading. It opens the section, and scrolls it into view,
    // when it is clicked open or asked for (gotoSection_).
    void section_fold_heading(Section s, const char* title, bool& open, bool first = false);
    void draw_model_section();
    void draw_parameter_table();
    void draw_fit_button();
    void draw_fit_section();
    void draw_fit_settings();
    void draw_data_table();

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
    // The height the graph needs at this width: at least a third of its
    // width, and the residual strip under it a quarter.
    float plot_min_height(float width) const;
    void draw_plot(const ImVec2& size);
    // The exact values of results data point i, in a tooltip.
    void point_tooltip(const kin::Analysis& a, int i);
    // Predictions from the fitted curve (formerly the Standard curve tab).
    void draw_prediction_box(const kin::Analysis& a, float width);
    // The Calculate box's answers as points on the curve, in the data's
    // units: [0] the X read off for the entered Y, [1] the Y for the entered X.
    std::array<std::optional<std::pair<double, double>>, 2> prediction_points(const kin::Analysis& a) const;
    void draw_runs_tab();
    void draw_report_tab();

    // ---- UI: dialogs (ui_dialogs.cpp) -----------------------------------
    void draw_dialogs();
    void draw_report_options_dialog();
    // The report options, on the Report tab and in the dialog.
    void draw_report_options_form();
    std::string report_options_summary() const;
    void draw_help_window();
    void draw_about_dialog();
    void draw_replace_dialog();

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
    bool dataOpen_ = true;            // the left panel's sections: unfolded?
    bool equationOpen_ = true;
    bool fitOpen_ = false;            // the fit settings: open when one is not the default
    std::optional<Section> gotoSection_;  // open it and bring it into view on the next frame
    float leftFooterHeight_ = 0.0f;   // Equation and Fit, below the data table (last frame)
    bool showEmptyHint_ = false;      // Fit was asked for with an empty table
    int textScale_ = 100;             // View > Text size, in percent
    float leftWidthEm_ = 26.0f;       // the left panel's width, in font heights (saved)
    bool firstFrame_ = true;

    // The runs; the one shown is worked on in the members below.
    std::vector<Run> runs_ = std::vector<Run>(1, Run{"Run 1", {}, "Entered by hand", {}, false, {}});
    int current_ = 0;
    std::filesystem::path analysisPath_;  // the analysis file, once saved or opened
    // The file the runs were read from, with what is needed to read it again
    // differently while the runs are as read (runsAsImported_).
    std::filesystem::path runsFile_;
    std::string runsText_;
    bool runsTable_ = false;  // a table of runs, rather than v3.51's format
    bool runsInRows_ = false;
    bool runsAsImported_ = false;

    // The run shown.
    std::vector<DataRow> rows_;
    std::string dataSource_ = "Entered by hand";
    std::optional<int> dataRun_;   // the classic interface's file run, when it loaded one
    std::filesystem::path tablePath_;
    std::string xUnit_, yUnit_;

    std::optional<kin::Analysis> analysis_;  // the results shown
    std::optional<int> analysisRun_;  // the classic interface's file run, when it fitted one
    bool stale_ = false;       // the equation or settings changed since the fit
    bool reportWritten_ = false;  // a report of the results was written

    // Replacing all the runs, once the user agrees.
    std::function<void()> pendingReplace_;
    std::string replaceWhat_;
    bool openReplace_ = false;
    int renameRun_ = -1;  // the run whose name is being edited in the Runs tab

    kin::PlotType plotType_ = kin::PlotType::Normal;
    kin::PlotOptions plotOptions_;
    bool numberPoints_ = true;
    bool refitPlot_ = true;
    bool residualStrip_ = true;     // residuals in a strip under the Normal plot
    float resultsFooterHeight_ = 0.0f;  // Residuals and history headers, below the plot (last frame)
    bool scrollToPlot_ = false;
    bool openResiduals_ = false;
    int residualScrollFrames_ = 0;  // scroll to the opened residuals once the layout has settled

    ImportState import_;  // the classic interface's imported file
    BatchState batch_;    // and its results
    ReportState report_;
    // The Calculate box's inputs, as typed: a Y to read X off the curve
    // for, and an X to read Y for. Blank for none.
    std::string predictFromY_, predictFromX_;
    // The data point under the mouse on the graph (an index into the
    // results' data), highlighted in the table on the next frame.
    int hoveredPoint_ = -1;
    int hoverNext_ = -1;

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
        std::function<void()> undo;  // puts back what the action changed
        std::string undoTooltip;
    };
    std::optional<Notice> notice_;
    bool openFewPoints_ = false;
    bool openReportSection_ = false;  // expand the Report tab's options on the next frame
    bool openReportOptions_ = false;
    bool openAbout_ = false;
    bool showHelp_ = false;
    int helpTopic_ = 0;
    enum class PendingReport { None, Current };
    PendingReport pendingReport_ = PendingReport::None;
    bool focusModel_ = false;
    bool focusTable_ = false;
    bool focusStdX_ = false;  // Calculate box: X for a given Y
    bool focusStdY_ = false;  // Calculate box: Y for a given X
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
