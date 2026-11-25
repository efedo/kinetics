#include "app/app.hpp"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <memory>
#include <utility>

#include "app/help_text.hpp"
#include "app/theme.hpp"
#include "app/ui_common.hpp"
#include "core/project.hpp"

namespace app {

// ===========================================================================
// Setup and settings
// ===========================================================================

void App::init(const std::filesystem::path& prefDir, const std::filesystem::path& exeDir) {
    prefDir_ = prefDir;
    exeDir_ = exeDir;
    rows_.assign(1, DataRow{});
    load_settings();
    const auto model = model_;
    set_model(model);
    stale_ = false;
    // Show the fit settings folded only when they hold nothing unusual.
    fitOpen_ = !fit_settings_are_default();
    requestedTab_ = Tab::Results;  // the getting-started page, until there are results
}

void App::shutdown() { save_settings(); }

void App::load_settings() {
    if (prefDir_.empty()) return;
    settings_.load(prefDir_ / "kinetics.ini");
    const auto& s = settings_;
    method_ = s.get_int("fit.method", 1) == 2 ? kin::Method::GaussNewton : kin::Method::Marquardt;
    guess_ = s.get_int("fit.guess", 1) == 2 ? kin::GuessMode::Manual : kin::GuessMode::Automatic;
    maxIterations_ = std::max(0, s.get_int("fit.maxIterations", 0));
    const auto letter = s.get("model", "a");
    if (auto m = kin::model_from_letter(letter.empty() ? 'a' : letter[0])) model_ = *m;
    auto& o = report_.options;
    o.format = static_cast<kin::ReportFormat>(std::clamp(s.get_int("report.format", 2), 0, 2));
    o.layout = static_cast<kin::ReportLayout>(std::clamp(s.get_int("report.layout", 0), 0, 1));
    o.header = s.get("report.header");
    o.experimentDate = s.get("report.date");
    o.numbering = static_cast<kin::Numbering>(std::clamp(s.get_int("report.numbering", 1), 0, 2));
    o.startNumber = s.get_int("report.start", 1);
    report_.file = s.get("report.file");
    report_.newFile = report_.file.empty() || !std::filesystem::exists(report_.file);
    report_.nextNumber = o.startNumber;
    numberPoints_ = s.get_bool("plot.numberPoints", true);
    residualStrip_ = s.get_bool("plot.residualStrip", true);
    xUnit_ = s.get("data.xUnit");
    yUnit_ = s.get("data.yUnit");
    import_.negativeIsMissing = s.get_bool("import.negativeIsMissing", true);
    import_.mpaCount = std::max(1, s.get_int("import.mpaCount", 8));
    if (auto st = style_from_name(s.get("view.style", "default"))) set_style(*st);
    if (auto t = theme_from_name(s.get("view.theme", "system"))) set_theme(*t);
    set_text_scale(s.get_int("view.textSize", 100));
    leftWidthEm_ = static_cast<float>(s.get_int("view.leftWidth", 260)) / 10.0f;
}

void App::save_settings() const {
    if (prefDir_.empty()) return;
    kin::Settings s = settings_;
    s.set_int("fit.method", static_cast<int>(method_));
    s.set_int("fit.guess", static_cast<int>(guess_));
    s.set_int("fit.maxIterations", maxIterations_);
    s.set("model", std::string(1, kin::model_def(model_).letter));
    const auto& o = report_.options;
    s.set_int("report.format", static_cast<int>(o.format));
    s.set_int("report.layout", static_cast<int>(o.layout));
    s.set("report.header", o.header);
    s.set("report.date", o.experimentDate);
    s.set_int("report.numbering", static_cast<int>(o.numbering));
    s.set_int("report.start", o.startNumber);
    s.set("report.file", report_.file.string());
    s.set_bool("plot.numberPoints", numberPoints_);
    s.set_bool("plot.residualStrip", residualStrip_);
    s.set("data.xUnit", xUnit_);
    s.set("data.yUnit", yUnit_);
    s.set_bool("import.negativeIsMissing", import_.negativeIsMissing);
    s.set_int("import.mpaCount", import_.mpaCount);
    s.set("view.style", style_name(style_choice()));
    s.set("view.theme", theme_name(theme_choice()));
    s.set_int("view.textSize", textScale_);
    s.set_int("view.leftWidth", static_cast<int>(std::lround(leftWidthEm_ * 10.0f)));
    std::error_code ec;
    std::filesystem::create_directories(prefDir_, ec);
    if (s.save(prefDir_ / "kinetics.ini")) persist_file(prefDir_ / "kinetics.ini");
}

std::string App::default_dir() const {
    const auto last = settings_.get("lastDir");
    if (!last.empty() && std::filesystem::is_directory(last)) return last + "/";
    const auto ex = exeDir_ / "examples";
    if (std::filesystem::is_directory(ex)) return ex.string() + "/";
    return {};
}

void App::show_window(const std::string& name) {
    if (name == "help") showHelp_ = true;
    else if (name == "about") openAbout_ = true;
    else if (name == "report-options") openReportOptions_ = true;
    else if (name == "fit-options") fitOpen_ = true;
    else if (name == "graph") {
        requestedTab_ = Tab::Results;
        scrollToPlot_ = true;
    } else if (name == "residuals") {
        requestedTab_ = Tab::Results;
        openResiduals_ = true;
    }
}

// ===========================================================================
// Model and data
// ===========================================================================

void App::set_model(kin::ModelId id) {
    model_ = id;
    params_ = kin::default_param_settings(id);
    const auto plots = kin::available_plots(id);
    if (std::find(plots.begin(), plots.end(), plotType_) == plots.end()) plotType_ = kin::PlotType::Normal;
    plotOptions_ = kin::default_plot_options(plotType_, id);
    refitPlot_ = true;
    mark_stale();
}

void App::set_plot_type(kin::PlotType t) {
    plotType_ = t;
    plotOptions_ = kin::default_plot_options(t, analysis_ ? analysis_->model() : model_);
    refitPlot_ = true;
}

bool App::v0_measured() const {
    const auto iv0 = kin::param_index(model_, kin::Role::V0);
    if (!iv0 || !params_[static_cast<size_t>(*iv0)].fixed) return false;
    for (const auto& p : table_points())
        if (p.x == 0.0 && std::isfinite(p.y)) return true;
    return false;
}

std::string App::report_file_tooltip() const {
    std::string s = "Add this report to the report file (Alt+P). ";
    if (kIsWeb) return s + "The whole report file is downloaded each time.";
    if (report_.file.empty()) return s + "You will be asked for a file.";
    return s + (report_.newFile ? "Starts " : "Appends to ") + report_.file.filename().string() + ".";
}

void App::set_text_scale(int percent) {
    textScale_ = std::clamp(percent, kTextScales.front(), kTextScales.back());
    ImGui::GetStyle().FontScaleMain = static_cast<float>(textScale_) / 100.0f;
}

void App::step_text_scale(int direction) {
    // The next listed size up or down from the current one.
    int next = textScale_;
    if (direction > 0) {
        for (int pct : kTextScales)
            if (pct > textScale_) {
                next = pct;
                break;
            }
    } else {
        for (int pct : kTextScales)
            if (pct < textScale_) next = pct;
    }
    if (next == textScale_) return;
    set_text_scale(next);
    save_settings();
}

std::string plain_model_label(kin::ModelId model, const std::vector<kin::ParamSetting>& params) {
    const auto& def = kin::model_def(model);
    std::string fixed;
    for (size_t j = 0; j < params.size() && j < def.params.size(); ++j)
        if (params[j].fixed) fixed += (fixed.empty() ? "" : ", ") + std::string(def.params[j].name);
    std::string s(def.menuName);
    if (!fixed.empty()) s += " (" + fixed + " fixed)";
    return s;
}

bool App::fit_settings_are_default() const {
    if (method_ != kin::Method::Marquardt || guess_ != kin::GuessMode::Automatic || maxIterations_ != 0) return false;
    const auto defaults = kin::default_param_settings(model_);
    for (size_t j = 0; j < params_.size() && j < defaults.size(); ++j)
        if (params_[j].fixed || params_[j].min != defaults[j].min || params_[j].max != defaults[j].max) return false;
    return true;
}

std::vector<kin::Point> App::table_points(int* invalidCells) const {
    int invalid = 0;
    std::vector<kin::Point> pts;
    for (const auto& r : rows_) {
        double x = NAN, y = NAN;
        const auto cx = parse_cell(r.x, x);
        const auto cy = parse_cell(r.y, y);
        invalid += (cx == CellState::Invalid) + (cy == CellState::Invalid);
        if (cx == CellState::Blank && cy == CellState::Blank) continue;
        pts.push_back({cx == CellState::Number ? x : NAN, cy == CellState::Number ? y : NAN});
    }
    if (invalidCells) *invalidCells = invalid;
    return pts;
}

void App::set_table(const std::vector<kin::Point>& pts) {
    dataOpen_ = true;
    // The same points again (the classic interface's data editor closed
    // without a change, a table already in order sorted): the results still
    // describe them, so they stay.
    const auto now = table_points();
    const auto same = [](double a, double b) { return a == b || (std::isnan(a) && std::isnan(b)); };
    if (now.size() == pts.size() &&
        std::equal(now.begin(), now.end(), pts.begin(),
                   [&](const kin::Point& a, const kin::Point& b) { return same(a.x, b.x) && same(a.y, b.y); }))
        return;
    rows_.clear();
    for (const auto& p : pts) rows_.push_back({number_text(p.x), number_text(p.y)});
    rows_.push_back({});
    mark_table_changed();
}

void App::sort_table() {
    auto pts = table_points();
    std::stable_sort(pts.begin(), pts.end(), [](const kin::Point& a, const kin::Point& b) {
        if (std::isnan(a.x)) return false;
        if (std::isnan(b.x)) return true;
        return a.x < b.x || (a.x == b.x && a.y < b.y);
    });
    set_table(pts);
}

App::TableSnapshot App::snapshot_table() const {
    TableSnapshot s;
    s.rows = rows_;
    while (!s.rows.empty() && s.rows.back().x.empty() && s.rows.back().y.empty()) s.rows.pop_back();
    s.source = dataSource_;
    s.run = dataRun_;
    s.analysis = analysis_;
    s.analysisRun = analysisRun_;
    s.stale = stale_;
    return s;
}

void App::restore_table(TableSnapshot s) {
    rows_ = std::move(s.rows);
    rows_.push_back({});
    mark_table_changed();  // also closes the message
    dataSource_ = std::move(s.source);
    dataRun_ = s.run;
    // The same data and results as before, so they are as current as they were.
    analysis_ = std::move(s.analysis);
    analysisRun_ = s.analysisRun;
    stale_ = s.stale;
    reportWritten_ = false;
    refitPlot_ = true;
}

void App::paste_table_from_clipboard() {
    // In the browser the text arrives later, through handle_file_results().
    if (auto text = request_clipboard_text()) apply_pasted_text(*text);
}

void App::apply_pasted_text(const std::string& text) {
    const auto pts = kin::parse_xy_table(text);
    if (pts.empty()) {
        error(kIsWeb && text.empty()
                  ? "The browser did not allow reading the clipboard, or it is empty. Allow clipboard access for "
                    "this page and try again."
                  : "The clipboard does not contain two columns of numbers.");
        return;
    }
    set_table(pts);
    dataSource_ = "Pasted from the clipboard";
    dataRun_.reset();
}

void App::open_table_file(const std::filesystem::path& path) {
    const auto text = kin::read_text_file(path);
    if (!text) {
        error("Can't open " + path.filename().string() + ".", path.string());
        return;
    }
    const auto pts = kin::parse_xy_table(*text);
    if (pts.empty()) {
        error(path.filename().string() + " does not contain two columns of numbers.", path.string());
        return;
    }
    // The run's table as it was can be put back from the status bar.
    auto before = snapshot_table();
    set_table(pts);
    tablePath_ = path;
    dataSource_ = path.filename().string();
    dataRun_.reset();
    if (!before.rows.empty()) {
        Notice n;
        n.text = path.filename().string() + " read into " + run_label(current_) + ".";
        n.undo = [this, before = std::move(before)]() mutable { restore_table(std::move(before)); };
        n.undoTooltip = "Put back the data the run had";
        notice_ = std::move(n);
    }
}

void App::open_example(Example e) {
    const auto ex = exeDir_ / "examples";
    // An example is a new analysis, in place of the data there is.
    auto table = [this](kin::ModelId model, std::filesystem::path path) {
        confirm_replace("Opening the example", [this, model, path]() {
            replace_runs({});
            analysisPath_.clear();
            set_model(model);
            open_table_file(path);
            notice_.reset();
            requestedTab_ = Tab::Results;
        });
    };
    switch (e) {
    case Example::MichaelisMenten: table(kin::ModelId::MichaelisMenten, ex / "michaelis_menten.csv"); break;
    case Example::Arrhenius: table(kin::ModelId::Arrhenius, ex / "arrhenius.csv"); break;
    case Example::HillRuns: import_runs_file(ex / "runs_in_columns.csv", true, kin::ModelId::HillGt0); break;
    }
}

void App::save_table(const std::filesystem::path& path) {
    const auto& def = kin::model_def(model_);
    if (!kin::write_text_file(path, kin::format_xy_table(table_points(), def.xName, def.yName), false)) {
        error("Can't write " + path.filename().string() + ".", path.string());
        return;
    }
    tablePath_ = path;
    dataSource_ = path.filename().string();
    file_written(path);
}

// ===========================================================================
// Units
// ===========================================================================

std::string App::unit_of(kin::Role role, kin::ModelId model) const {
    return kin::parameter_unit(model, role, xUnit_, yUnit_);
}

std::string App::x_heading(kin::ModelId model) const {
    const auto& def = kin::model_def(model);
    const bool fixedUnit = def.family == kin::Family::Arrhenius;  // T (°C)
    return std::string(def.xName) + (xUnit_.empty() || fixedUnit ? std::string() : " (" + xUnit_ + ")");
}

std::string App::y_heading(kin::ModelId model) const {
    const auto& def = kin::model_def(model);
    const bool log = def.family == kin::Family::Arrhenius;  // log10(v)
    return std::string(def.yName) + (yUnit_.empty() || log ? std::string() : " (" + yUnit_ + ")");
}

// ===========================================================================
// Fitting
// ===========================================================================

kin::AnalysisRequest App::make_request(std::vector<kin::Point> data) const {
    kin::AnalysisRequest r;
    r.model = model_;
    r.data = std::move(data);
    r.params = params_;
    r.method = method_;
    r.guess = guess_;
    r.maxIterations = maxIterations_;
    return r;
}

void App::request_fit() {
    // F5 fits the run shown (Fit all runs, on the Runs tab, fits them all).
    const auto pts = kin::clean_data(table_points());
    if (pts.empty()) {
        // A note above the table says what to do, and the cursor goes there.
        showEmptyHint_ = true;
        focusTable_ = true;
        return;
    }
    if (kin::too_few_points(model_, params_, pts.size())) {
        openFewPoints_ = true;  // ask before fitting, as v3.51 did
        return;
    }
    fit_now();
}

void App::fit_now() { apply_analysis(kin::analyse(make_request(table_points())), dataRun_); }

void App::apply_analysis(kin::AnalysisOutcome outcome, std::optional<int> runNumber) {
    auto& run = runs_[static_cast<size_t>(current_)];
    if (!outcome.analysis) {
        run.error = outcome.error;
        error(outcome.error);
        return;
    }
    run.error.clear();
    analysis_ = std::move(outcome.analysis);
    analysisRun_ = runNumber;
    stale_ = false;
    reportWritten_ = false;
    refitPlot_ = true;
    notice_.reset();
    const auto plots = kin::available_plots(analysis_->model());
    if (std::find(plots.begin(), plots.end(), plotType_) == plots.end()) set_plot_type(kin::PlotType::Normal);
    // A fit that did not converge may need other options: show them.
    if (!analysis_->linear && analysis_->fit.status != kin::FitStatus::Converged) fitOpen_ = true;
}

// ===========================================================================
// Runs
// ===========================================================================

namespace {

// A run's points from its table rows (see App::table_points).
std::vector<kin::Point> points_of(const std::vector<DataRow>& rows, int* invalidCells = nullptr) {
    std::vector<kin::Point> pts;
    int invalid = 0;
    for (const auto& r : rows) {
        double x = NAN, y = NAN;
        const auto cx = parse_cell(r.x, x);
        const auto cy = parse_cell(r.y, y);
        invalid += (cx == CellState::Invalid) + (cy == CellState::Invalid);
        if (cx == CellState::Blank && cy == CellState::Blank) continue;
        pts.push_back({cx == CellState::Number ? x : NAN, cy == CellState::Number ? y : NAN});
    }
    if (invalidCells) *invalidCells = invalid;
    return pts;
}

std::vector<DataRow> rows_of(const std::vector<kin::Point>& pts) {
    std::vector<DataRow> rows;
    for (const auto& p : pts) rows.push_back({number_text(p.x), number_text(p.y)});
    rows.push_back({});
    return rows;
}

Run new_run(int number) { return Run{"Run " + std::to_string(number), {DataRow{}}, "Entered by hand", {}, false, {}}; }

}  // namespace

void App::commit_run() {
    auto& r = runs_[static_cast<size_t>(current_)];
    r.rows = rows_;
    r.source = dataSource_;
    r.analysis = analysis_;
    r.stale = stale_;
}

void App::load_run(int k) {
    current_ = std::clamp(k, 0, static_cast<int>(runs_.size()) - 1);
    const auto& r = runs_[static_cast<size_t>(current_)];
    rows_ = r.rows;
    if (rows_.empty() || !rows_.back().x.empty() || !rows_.back().y.empty()) rows_.push_back({});
    dataSource_ = r.source;
    analysis_ = r.analysis;
    stale_ = r.stale;
    dataRun_.reset();
    analysisRun_.reset();
    reportWritten_ = false;
    refitPlot_ = true;
    hoveredPoint_ = hoverNext_ = -1;
}

void App::select_run(int k) {
    if (k < 0 || k >= static_cast<int>(runs_.size())) return;
    if (k == current_) return;
    commit_run();
    load_run(k);
    notice_.reset();  // its Undo would act on the run left
    if (analysis_) {
        const auto plots = kin::available_plots(analysis_->model());
        if (std::find(plots.begin(), plots.end(), plotType_) == plots.end()) set_plot_type(kin::PlotType::Normal);
    }
}

void App::add_run() {
    commit_run();
    runs_.push_back(new_run(static_cast<int>(runs_.size()) + 1));
    load_run(static_cast<int>(runs_.size()) - 1);
    runsAsImported_ = false;  // reading the file again would lose the run
    notice_.reset();
    dataOpen_ = true;
    focusTable_ = true;
    gotoSection_ = Section::Data;
}

void App::delete_run(int k) {
    if (runs_.size() < 2 || k < 0 || k >= static_cast<int>(runs_.size())) return;
    commit_run();
    const int shown = current_;
    const std::string label = run_label(k);
    Run removed = runs_[static_cast<size_t>(k)];
    runs_.erase(runs_.begin() + k);
    runsAsImported_ = false;
    // The run shown stays shown, unless it is the one deleted.
    load_run(k < shown ? shown - 1 : std::min(shown, static_cast<int>(runs_.size()) - 1));
    Notice n;
    n.text = "Deleted " + label + ".";
    n.undoTooltip = "Put the run back";
    n.undo = [this, k, shown, removed]() {
        commit_run();
        runs_.insert(runs_.begin() + std::min(k, static_cast<int>(runs_.size())), removed);
        load_run(shown);
    };
    notice_ = std::move(n);
}

void App::clear_all_runs() {
    commit_run();
    const size_t count = runs_.size();
    // Everything the runs bring with them, for Undo.
    struct Saved {
        std::vector<Run> runs;
        int shown;
        std::filesystem::path file;
        std::string text;
        bool table, inRows, asImported;
        std::string calcInput;
        bool calcInputIsX;
    };
    auto before = std::make_shared<Saved>(Saved{runs_, current_, runsFile_, runsText_, runsTable_, runsInRows_,
                                                runsAsImported_, calcInput_, calcInputIsX_});
    replace_runs({});
    requestedTab_ = Tab::Runs;
    Notice n;
    n.text = "Cleared " + std::string(count == 1 ? "the run" : "all " + std::to_string(count) + " runs") + ".";
    n.undoTooltip = count == 1 ? "Put the run back" : "Put the runs back";
    n.undo = [this, before]() {
        runs_ = before->runs;
        runsFile_ = before->file;
        runsText_ = before->text;
        runsTable_ = before->table;
        runsInRows_ = before->inRows;
        runsAsImported_ = before->asImported;
        calcInput_ = before->calcInput;
        calcInputIsX_ = before->calcInputIsX;
        load_run(before->shown);
    };
    notice_ = std::move(n);  // after replace_runs, which dismisses messages
}

void App::fit_run(int k) {
    select_run(k);
    request_fit();
}

void App::fit_all_runs() {
    commit_run();
    const auto tmpl = make_request({});
    int fitted = 0, failed = 0, empty = 0;
    for (auto& r : runs_) {
        auto pts = points_of(r.rows);
        if (kin::clean_data(pts).empty()) {
            ++empty;
            continue;
        }
        auto request = tmpl;
        request.data = std::move(pts);
        auto outcome = kin::analyse(request);
        if (outcome.analysis) {
            r.analysis = std::move(outcome.analysis);
            r.stale = false;
            r.error.clear();
            ++fitted;
        } else {
            r.error = outcome.error;
            ++failed;
        }
    }
    load_run(current_);
    std::string s = "Fitted " + std::to_string(fitted) + " run" + (fitted == 1 ? "" : "s");
    if (failed > 0) s += "; " + std::to_string(failed) + " could not be fitted (see the Runs tab)";
    if (empty > 0) s += "; " + std::to_string(empty) + " without data";
    info(s + ".");
    if (failed > 0) notice_->isError = true;
    if (analysis_) {
        const auto plots = kin::available_plots(analysis_->model());
        if (std::find(plots.begin(), plots.end(), plotType_) == plots.end()) set_plot_type(kin::PlotType::Normal);
    }
}

std::string App::run_own_name(int k) const {
    if (k < 0 || k >= static_cast<int>(runs_.size())) return {};
    const auto& name = runs_[static_cast<size_t>(k)].name;
    // A plain number, or "Run" and a number ("Run 4", "run #4"), is a run
    // number rather than a name: runs are numbered by their place.
    std::string rest;
    for (char c : name) rest += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (rest.rfind("run", 0) == 0) rest.erase(0, 3);
    rest.erase(0, rest.find_first_not_of(" #"));
    const bool number = !rest.empty() && rest.find_first_not_of("0123456789") == std::string::npos;
    if (name.empty() || number) return {};
    return name;
}

std::string App::run_label(int k) const {
    const std::string own = run_own_name(k);
    return "Run " + std::to_string(k + 1) + (own.empty() ? "" : ": " + own);
}

int App::run_point_count(int k) const {
    const auto& rows = k == current_ ? rows_ : runs_[static_cast<size_t>(k)].rows;
    return static_cast<int>(kin::clean_data(points_of(rows)).size());
}

const kin::Analysis* App::run_analysis(int k) const {
    if (k == current_) return analysis_ ? &*analysis_ : nullptr;
    const auto& a = runs_[static_cast<size_t>(k)].analysis;
    return a ? &*a : nullptr;
}

bool App::run_stale(int k) const { return k == current_ ? stale_ : runs_[static_cast<size_t>(k)].stale; }

bool App::any_results() const {
    for (int k = 0; k < static_cast<int>(runs_.size()); ++k)
        if (run_analysis(k)) return true;
    return false;
}

bool App::have_data() const {
    for (int k = 0; k < static_cast<int>(runs_.size()); ++k)
        for (const auto& p : points_of(k == current_ ? rows_ : runs_[static_cast<size_t>(k)].rows))
            if (std::isfinite(p.x) || std::isfinite(p.y)) return true;
    return false;
}

std::optional<int> App::report_run_number(int k) const {
    if (runs_.size() > 1) return k + 1;
    return analysisRun_;
}

void App::replace_runs(std::vector<Run> runs) {
    if (runs.empty()) runs.push_back(new_run(1));
    runs_ = std::move(runs);
    load_run(0);
    runsFile_.clear();
    runsText_.clear();
    runsAsImported_ = false;
    notice_.reset();
    dataOpen_ = true;
    calcInput_.clear();
}

void App::confirm_replace(const std::string& what, std::function<void()> action) {
    if (!have_data()) {
        action();
        return;
    }
    replaceWhat_ = what;
    pendingReplace_ = std::move(action);
    openReplace_ = true;
}

void App::new_analysis() {
    confirm_replace("A new analysis", [this]() {
        replace_runs({});
        analysisPath_.clear();
        requestedTab_ = Tab::Results;
    });
}

// Runs from a parsed file of runs.
static std::vector<Run> runs_from(const kin::SpreadsheetFile& f, bool negativeIsMissing, const std::string& source) {
    std::vector<Run> runs;
    for (const auto& r : f.runs) {
        Run run;
        run.name = r.name.empty() ? "Run " + std::to_string(runs.size() + 1) : r.name;
        run.rows = rows_of(f.points(r, negativeIsMissing));
        run.source = source;
        runs.push_back(std::move(run));
    }
    return runs;
}

void App::import_runs_file(const std::filesystem::path& path, bool ask, std::optional<kin::ModelId> model) {
    const auto text = kin::read_text_file(path);
    const std::string name = path.filename().string();
    if (!text) {
        error("Can't open " + name + ".", path.string());
        return;
    }
    // A saved analysis, by its first line.
    if (kin::parse_project(*text).project) {
        open_analysis_file(path, ask);
        return;
    }
    // v3.51's print-to-file format, or a table with a run in each column.
    const bool table = !kin::is_v351_spreadsheet(*text);
    const auto parsed = table ? kin::parse_run_table(*text, false) : kin::parse_spreadsheet(*text);
    if (!parsed.file) {
        error(name + ": " + parsed.error, path.string());
        return;
    }
    auto runs = runs_from(*parsed.file, import_.negativeIsMissing, name);
    auto apply = [this, path, text = *text, table, runs = std::move(runs), model]() mutable {
        replace_runs(std::move(runs));
        if (model) set_model(*model);
        runsFile_ = path;
        runsText_ = std::move(text);
        runsTable_ = table;
        runsInRows_ = false;
        runsAsImported_ = true;
        analysisPath_.clear();
        requestedTab_ = Tab::Runs;
    };
    if (ask) confirm_replace("Importing " + name, std::move(apply));
    else apply();
}

void App::reread_runs_file() {
    if (!runsAsImported_ || runsText_.empty()) return;
    const std::string name = runsFile_.filename().string();
    const auto parsed = runsTable_ ? kin::parse_run_table(runsText_, runsInRows_) : kin::parse_spreadsheet(runsText_);
    if (!parsed.file) {
        error(name + ": " + parsed.error, runsFile_.string());
        runsInRows_ = !runsInRows_;  // the choice that read the file
        return;
    }
    const auto file = runsFile_;
    auto text = std::move(runsText_);
    const bool table = runsTable_, rows = runsInRows_;
    replace_runs(runs_from(*parsed.file, import_.negativeIsMissing, name));
    runsFile_ = file;
    runsText_ = std::move(text);
    runsTable_ = table;
    runsInRows_ = rows;
    runsAsImported_ = true;
}

void App::open_analysis_file(const std::filesystem::path& path, bool ask) {
    const auto text = kin::read_text_file(path);
    const std::string name = path.filename().string();
    if (!text) {
        error("Can't open " + name + ".", path.string());
        return;
    }
    auto parsed = kin::parse_project(*text);
    if (!parsed.project) {
        error(name + ": " + parsed.error, path.string());
        return;
    }
    auto apply = [this, path, p = std::move(*parsed.project)]() {
        std::vector<Run> runs;
        std::vector<bool> fitted;
        for (const auto& r : p.runs) {
            Run run;
            run.name = r.name;
            run.rows = rows_of(r.points);
            run.source = path.filename().string();
            runs.push_back(std::move(run));
            fitted.push_back(r.fitted);
        }
        replace_runs(std::move(runs));
        set_model(p.model);
        params_ = p.params;
        method_ = p.method;
        guess_ = p.guess;
        maxIterations_ = p.maxIterations;
        xUnit_ = p.xUnit;
        yUnit_ = p.yUnit;
        fitOpen_ = !fit_settings_are_default();
        // The runs that had results have them again: the same data and
        // settings give the same fit.
        const auto tmpl = make_request({});
        for (size_t k = 0; k < runs_.size(); ++k) {
            if (!fitted[k]) continue;
            auto request = tmpl;
            request.data = points_of(runs_[k].rows);
            auto outcome = kin::analyse(request);
            if (outcome.analysis) runs_[k].analysis = std::move(outcome.analysis);
            else runs_[k].error = outcome.error;
        }
        load_run(0);
        analysisPath_ = path;
        requestedTab_ = Tab::Results;
        save_settings();
        info("Opened " + path.filename().string() + ".", path.string());
    };
    if (ask) confirm_replace("Opening " + name, std::move(apply));
    else apply();
}

void App::save_analysis(const std::filesystem::path& path) {
    commit_run();
    kin::Project p;
    p.model = model_;
    p.params = params_;
    p.method = method_;
    p.guess = guess_;
    p.maxIterations = maxIterations_;
    p.xUnit = xUnit_;
    p.yUnit = yUnit_;
    for (const auto& r : runs_)
        p.runs.push_back(kin::ProjectRun{r.name, points_of(r.rows), r.analysis.has_value()});
    if (!kin::write_text_file(path, kin::format_project(p), false)) {
        error("Can't write " + path.filename().string() + ".", path.string());
        return;
    }
    analysisPath_ = path;
    file_written(path);
    info("Saved the analysis as " + path.filename().string() + ".", path.string());
}

void App::ask_analysis_file() {
    const std::string name = !analysisPath_.empty() ? analysisPath_.filename().string()
                             : !runsFile_.empty()   ? runsFile_.stem().string() + ".kinetics"
                                                    : std::string("analysis.kinetics");
    show_save_dialog(FilePurpose::SaveAnalysis, default_dir() + name);
}

void App::save_analysis_or_ask() {
    // In the browser every save is a download, named each time.
    if (analysisPath_.empty() || kIsWeb) ask_analysis_file();
    else save_analysis(analysisPath_);
}

void App::export_runs(const std::filesystem::path& path) {
    commit_run();
    std::vector<kin::NamedPoints> runs;
    for (int k = 0; k < static_cast<int>(runs_.size()); ++k)
        runs.push_back({run_own_name(k).empty() ? "Run " + std::to_string(k + 1) : runs_[static_cast<size_t>(k)].name,
                        points_of(runs_[static_cast<size_t>(k)].rows)});
    if (!kin::write_text_file(path, kin::format_run_table(runs, x_heading(model_)), false)) {
        error("Can't write " + path.filename().string() + ".", path.string());
        return;
    }
    file_written(path);
    info("Saved " + std::to_string(runs.size()) + " run" + (runs.size() == 1 ? "" : "s") + " as " +
             path.filename().string() + ", a table with a run in each column.",
         path.string());
}

// ===========================================================================
// The classic interface's file of runs
// ===========================================================================

void App::import_spreadsheet(const std::filesystem::path& path) {
    const auto text = kin::read_text_file(path);
    if (!text) {
        error("Can't open " + path.filename().string() + ".", path.string());
        return;
    }
    // v3.51's print-to-file format, or a table with a run in each column.
    const bool table = !kin::is_v351_spreadsheet(*text);
    auto parsed = table ? kin::parse_run_table(*text, false) : kin::parse_spreadsheet(*text);
    if (!parsed.file) {
        error(path.filename().string() + ": " + parsed.error, path.string());
        return;
    }
    import_.type = ImportType::Spreadsheet;
    import_.path = path;
    import_.sheet = std::move(parsed.file);
    import_.table = table;
    import_.runsInRows = false;
    import_.tableText = table ? *text : std::string();
    import_.mpaText.clear();
    import_.selectedRun = import_.sheet->runs.front().number;
    batch_ = BatchState{};
    batch_.firstRun = import_.selectedRun;
}

void App::import_mpa(const std::filesystem::path& path) {
    const auto text = kin::read_text_file(path);
    if (!text) {
        error("Can't open " + path.filename().string() + ".", path.string());
        return;
    }
    import_.type = ImportType::Mpa;
    import_.path = path;
    import_.sheet.reset();
    import_.mpaText = *text;
    import_.selectedRun = 1;
    batch_ = BatchState{};
}

// ===========================================================================
// Reports
// ===========================================================================

kin::ReportContext App::report_context(std::optional<int> runNumber, bool firstInFile) const {
    kin::ReportContext ctx;
    ctx.sequenceNumber = report_.nextNumber;
    ctx.runNumber = runNumber;
    ctx.today = kin::today_string();
    ctx.firstInFile = firstInFile;
    ctx.xUnit = xUnit_;
    ctx.yUnit = yUnit_;
    return ctx;
}

// The report of every run that has results, one after another (one line
// each in the short format).
std::string App::current_report_text(bool forFile) const {
    const bool first = forFile ? (report_.newFile || !std::filesystem::exists(report_.file) ||
                                  std::filesystem::file_size(report_.file) == 0)
                               : true;
    std::string text;
    int n = 0;
    for (int k = 0; k < static_cast<int>(runs_.size()); ++k) {
        const auto* a = run_analysis(k);
        if (!a) continue;
        auto ctx = report_context(report_run_number(k), first && n == 0);
        ctx.sequenceNumber = report_.nextNumber + n;
        text += kin::format_report(*a, report_.options, ctx);
        ++n;
    }
    return text;
}

void App::choose_report_file() {
    show_save_dialog(FilePurpose::ReportFile,
                     report_.file.empty() ? default_dir() + "results.txt" : report_.file.string());
}

bool App::append_report(const kin::Analysis& a, std::optional<int> runNumber, bool first) {
    std::error_code ec;
    const bool empty = !std::filesystem::exists(report_.file, ec) || std::filesystem::file_size(report_.file, ec) == 0;
    const auto text =
        kin::format_report(a, report_.options, report_context(runNumber, first && (report_.newFile || empty)));
    if (!kin::write_text_file(report_.file, text, !report_.newFile)) {
        error("Can't write the report file " + report_.file.filename().string() + ".", report_.file.string());
        return false;
    }
    report_.newFile = false;
    ++report_.nextNumber;
    return true;
}

void App::write_report() {
    if (!any_results()) {
        error("There are no results to report yet: fit the data first (F5).");
        return;
    }
    if (report_.file.empty()) {
        pendingReport_ = PendingReport::Current;
        choose_report_file();
        return;
    }
    // Every run with results, in order.
    int written = 0;
    for (int k = 0; k < static_cast<int>(runs_.size()); ++k) {
        const auto* a = run_analysis(k);
        if (!a) continue;
        if (!append_report(*a, report_run_number(k), written == 0)) return;
        ++written;
    }
    report_.status = (written == 1 ? std::string("Report") : std::to_string(written) + " reports") + " written to " +
                     report_.file.filename().string() + ".";
    file_written(report_.file);
    reportWritten_ = true;
    info(report_.status, report_.file.string());  // seen from any tab (Alt+P)
}

void App::copy_report() {
    const auto text = current_report_text(false);
    if (text.empty()) {
        error("There are no results to report yet: fit the data first (F5).");
        return;
    }
    ImGui::SetClipboardText(text.c_str());
    report_.status = "Report copied to the clipboard.";
    info(report_.status);
}

void App::copy_results() {
    if (!analysis_) return;
    const auto& a = *analysis_;
    const auto& def = kin::model_def(a.model());
    // A short table with every digit, tab-separated for a spreadsheet.
    std::string t = plain_model_label(a.model(), a.request.params) + ", " + std::to_string(a.fit.n) + " points";
    if (runs_.size() > 1) t += ", " + run_label(current_);
    else if (analysisRun_) t += ", run #" + std::to_string(*analysisRun_);
    t += "\nParameter\tValue\tSD\tRelative SD (%)\tUnit\n";
    const auto rows = a.parameter_rows();
    for (size_t i = 0; i < rows.size(); ++i) {
        const auto& r = rows[i];
        t += r.name + "\t" + kin::fmt_g(r.value) + "\t";
        if (r.fixed) t += "(fixed)\t";
        else t += kin::fmt_g(r.sd) + "\t" + (r.value != 0.0 ? kin::fmt_g(std::fabs(r.sd * 100.0 / r.value), 3) : "");
        t += "\t";
        if (i < def.params.size()) t += unit_of(def.params[i].role, a.model());
        t += "\n";
    }
    t += "Residual SD\t" + kin::fmt_g(a.fit.stdDev) + "\t\t\t" + (def.family == kin::Family::Arrhenius ? "" : yUnit_) + "\n";
    // Then each point with its fitted value and residual.
    t += "\nPoint\t" + x_heading(a.model()) + "\t" + y_heading(a.model()) + "\tFitted " + std::string(def.yName) +
         "\tResidual\n";
    for (size_t i = 0; i < a.data.size(); ++i) {
        t += std::to_string(i + 1) + "\t" + kin::fmt_g(a.data[i].x) + "\t" + kin::fmt_g(a.data[i].y) + "\t";
        if (i < a.yhat.size()) t += kin::fmt_g(a.yhat[i]);
        t += "\t";
        if (i < a.resid.size()) t += kin::fmt_g(a.resid[i]);
        t += "\n";
    }
    ImGui::SetClipboardText(t.c_str());
    info("Results copied to the clipboard, as tables for a spreadsheet.");
}

void App::clear_table() {
    auto before = snapshot_table();
    if (before.rows.empty()) return;  // nothing to clear
    const size_t count = before.rows.size();
    set_table({});
    dataSource_ = "Entered by hand";
    dataRun_.reset();
    Notice n;
    n.text = "Cleared the table (" + std::to_string(count) + " row" + (count == 1 ? "" : "s") + ").";
    n.undo = [this, before = std::move(before)]() mutable { restore_table(std::move(before)); };
    n.undoTooltip = "Put the cleared rows back";
    notice_ = std::move(n);  // after set_table, which dismisses messages
}

// ===========================================================================
// Dialog results, messages
// ===========================================================================

void App::error(const std::string& message, const std::string& detail) {
    notice_ = Notice{};
    notice_->isError = true;
    notice_->text = message;
    notice_->detail = detail;
}

void App::info(const std::string& message, const std::string& detail) {
    notice_ = Notice{};
    notice_->text = message;
    notice_->detail = detail;
}

void App::handle_file_results() {
    if (auto text = take_clipboard_text()) apply_pasted_text(*text);
    for (const auto& r : take_file_dialog_results()) {
        const std::filesystem::path path = path_from_utf8(r.path);
        settings_.set("lastDir", path.parent_path().string());
        switch (r.purpose) {
            case FilePurpose::OpenTable: open_table_file(path); break;
            case FilePurpose::SaveTable: save_table(path); break;
            case FilePurpose::ImportSpreadsheet: import_spreadsheet(path); break;
            case FilePurpose::ImportMpa: import_mpa(path); break;
            case FilePurpose::ImportRuns: import_runs_file(path); break;
            case FilePurpose::ExportRuns: export_runs(path); break;
            case FilePurpose::OpenAnalysis: open_analysis_file(path); break;
            case FilePurpose::SaveAnalysis: save_analysis(path); break;
            case FilePurpose::ReportFile: {
                report_.file = path;
                std::error_code ec;
                report_.newFile = !std::filesystem::exists(path, ec);
                report_.nextNumber = report_.options.startNumber;
                save_settings();  // at once: a browser tab may be closed without a save
                const auto pending = pendingReport_;
                pendingReport_ = PendingReport::None;
                if (pending == PendingReport::Current) write_report();
                break;
            }
        }
    }
}

}  // namespace app
