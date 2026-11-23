#include "app/app.hpp"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>

#include "app/help_text.hpp"
#include "app/theme.hpp"
#include "app/ui_common.hpp"

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
    stale_ = batchStale_ = false;
    // Show the fit settings folded only when they hold nothing unusual.
    fitOpen_ = !fit_settings_are_default();
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
    showFittedValues_ = s.get_bool("plot.fittedValues", true);
    residualStrip_ = s.get_bool("plot.residualStrip", true);
    xUnit_ = s.get("data.xUnit");
    yUnit_ = s.get("data.yUnit");
    import_.negativeIsMissing = s.get_bool("import.negativeIsMissing", true);
    import_.mpaCount = std::max(1, s.get_int("import.mpaCount", 8));
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
    s.set_bool("plot.fittedValues", showFittedValues_);
    s.set_bool("plot.residualStrip", residualStrip_);
    s.set("data.xUnit", xUnit_);
    s.set("data.yUnit", yUnit_);
    s.set_bool("import.negativeIsMissing", import_.negativeIsMissing);
    s.set_int("import.mpaCount", import_.mpaCount);
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

bool App::results_stale() const {
    if (!analysis_) return false;
    return showing_run() ? batchStale_ : stale_;
}

void App::close_import() {
    leave_run_view();  // back to the table's own results
    ImportState fresh;
    fresh.negativeIsMissing = import_.negativeIsMissing;  // preferences, not the file
    fresh.mpaCount = import_.mpaCount;
    import_ = std::move(fresh);
    batch_ = BatchState{};
    batchStale_ = false;
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
    std::vector<kin::Point> pts;
    int invalid = 0;
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
    leave_run_view();  // what changes is the table, so show it
    rows_.clear();
    for (const auto& p : pts) rows_.push_back({number_text(p.x), number_text(p.y)});
    rows_.push_back({});
    mark_table_changed();
}

void App::set_table_y(const std::vector<double>& ys) {
    leave_run_view();
    // Keep existing X values (MPA files carry velocities only).
    while (!rows_.empty() && rows_.back().x.empty() && rows_.back().y.empty()) rows_.pop_back();
    if (rows_.size() < ys.size()) rows_.resize(ys.size());
    for (size_t i = 0; i < rows_.size(); ++i) rows_[i].y = i < ys.size() ? number_text(ys[i]) : std::string();
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
    leave_run_view();
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
    set_table(pts);
    tablePath_ = path;
    dataSource_ = path.filename().string();
    dataRun_.reset();
}

void App::open_example(Example e) {
    const auto ex = exeDir_ / "examples";
    switch (e) {
    case Example::MichaelisMenten:
        set_model(kin::ModelId::MichaelisMenten);
        open_table_file(ex / "michaelis_menten.csv");
        break;
    case Example::Arrhenius:
        set_model(kin::ModelId::Arrhenius);
        open_table_file(ex / "arrhenius.csv");
        break;
    case Example::SpreadsheetRuns:
        set_model(kin::ModelId::HillGt0);
        import_spreadsheet(ex / "spreadsheet_runs.txt");
        break;
    case Example::MpaPlate:
        set_model(kin::ModelId::InhibitionLinear);
        set_table({{0, NAN}, {0.5, NAN}, {1, NAN}, {2, NAN}, {4, NAN}, {8, NAN}, {16, NAN}, {32, NAN}});
        dataSource_ = "[I] values for the MPA example";
        import_mpa(ex / "mpa_plate.txt");
        load_selected_run_into_table();
        notice_.reset();  // nothing of the user's was replaced
        break;
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
    // F5 fits what is on screen: while a file run is shown, the file's runs,
    // once the settings have changed (fitting them again unchanged would only
    // repeat any reports).
    if (showing_run()) {
        if (batchStale_) refit_runs();
        else info("The runs are already fitted with these settings. To fit your own table, press Back to my table.");
        return;
    }
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

void App::fit_now() {
    leave_run_view();
    apply_analysis(kin::analyse(make_request(table_points())), dataRun_);
}

void App::apply_analysis(kin::AnalysisOutcome outcome, std::optional<int> runNumber) {
    if (!outcome.analysis) {
        error(outcome.error);
        return;
    }
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
// File runs
// ===========================================================================

void App::import_spreadsheet(const std::filesystem::path& path) {
    const auto text = kin::read_text_file(path);
    if (!text) {
        error("Can't open " + path.filename().string() + ".", path.string());
        return;
    }
    auto parsed = kin::parse_spreadsheet(*text);
    if (!parsed.file) {
        error(path.filename().string() + ": " + parsed.error, path.string());
        return;
    }
    leave_run_view();
    import_.type = ImportType::Spreadsheet;
    import_.path = path;
    import_.sheet = std::move(parsed.file);
    import_.mpaText.clear();
    import_.selectedRun = import_.sheet->runs.front().number;
    batch_ = BatchState{};
    batch_.firstRun = import_.selectedRun;
    batchStale_ = false;
    requestedTab_ = Tab::FileRuns;
}

void App::import_mpa(const std::filesystem::path& path) {
    const auto text = kin::read_text_file(path);
    if (!text) {
        error("Can't open " + path.filename().string() + ".", path.string());
        return;
    }
    leave_run_view();
    import_.type = ImportType::Mpa;
    import_.path = path;
    import_.sheet.reset();
    import_.mpaText = *text;
    import_.selectedRun = 1;
    batch_ = BatchState{};
    batchStale_ = false;
    requestedTab_ = Tab::FileRuns;
}

bool App::load_selected_run_into_table() {
    const auto name = import_.path.filename().string();
    leave_run_view();
    // The table it replaces can be put back from the status bar.
    const auto before = snapshot_table();
    std::string what;
    if (import_.type == ImportType::Spreadsheet) {
        const auto* run = import_.sheet->find_run(import_.selectedRun);
        if (!run) {
            error("Run #" + std::to_string(import_.selectedRun) + " not found in " + name + ".");
            return false;
        }
        set_table(import_.sheet->points(*run, import_.negativeIsMissing));
        dataSource_ = "Run #" + std::to_string(run->number) + " of " + name;
        dataRun_ = run->number;
        what = "Run #" + std::to_string(run->number);
    } else if (import_.type == ImportType::Mpa) {
        const auto ds = kin::parse_mpa_dataset(import_.mpaText, import_.selectedRun, import_.mpaCount);
        if (!ds.error.empty()) {
            error(name + ": " + ds.error);
            return false;
        }
        set_table_y(ds.y);
        dataSource_ = "Dataset #" + std::to_string(import_.selectedRun) + " of " + name + " (velocities)";
        dataRun_ = import_.selectedRun;
        what = "The velocities of dataset #" + std::to_string(import_.selectedRun);
    } else {
        error("No file is imported. Use File > Import spreadsheet file of runs, or Import plate-reader file.");
        return false;
    }
    if (!before.rows.empty()) {
        Notice n;
        n.text = what + " of " + name + " copied into the table.";
        n.undo = before;
        n.undoTooltip = "Put back the table as it was";
        notice_ = std::move(n);
    }
    return true;
}

void App::fit_selected_run() {
    if (!load_selected_run_into_table()) return;
    const auto undo = std::exchange(notice_, std::nullopt);
    request_fit();
    if (!notice_ && undo) notice_ = undo;  // keep the Undo unless the fit had something to say
}

void App::analyse_all_runs() {
    const auto tmpl = make_request({});
    std::vector<kin::BatchItem> items;
    if (import_.type == ImportType::Spreadsheet) {
        items = kin::analyse_spreadsheet_runs(*import_.sheet, batch_.firstRun, tmpl, import_.negativeIsMissing);
        if (items.empty()) {
            error("Run #" + std::to_string(batch_.firstRun) + " not found in " + import_.path.filename().string() + ".");
            return;
        }
    } else if (import_.type == ImportType::Mpa) {
        std::vector<double> xs;
        for (const auto& r : rows_) {
            double x = NAN;
            if (parse_cell(r.x, x) == CellState::Number) xs.push_back(x);
            else if (!r.x.empty() || !r.y.empty()) xs.push_back(NAN);
        }
        int have = 0;
        for (int i = 0; i < import_.mpaCount && i < static_cast<int>(xs.size()); ++i) have += std::isfinite(xs[static_cast<size_t>(i)]);
        if (have < import_.mpaCount) {
            error("MPA files hold velocities only: enter the " + std::to_string(import_.mpaCount) +
                  " concentrations in the X column of the data table first.");
            return;
        }
        xs.resize(static_cast<size_t>(import_.mpaCount));
        items = kin::analyse_mpa_datasets(import_.mpaText, batch_.firstRun, import_.mpaCount, xs, tmpl);
        if (items.empty()) {
            error("Dataset #" + std::to_string(batch_.firstRun) + " not found in " + import_.path.filename().string() + ".");
            return;
        }
    } else {
        error("No file is imported. Use File > Import spreadsheet file of runs, or Import plate-reader file.");
        return;
    }
    // The new list replaces the one any shown run came from.
    leave_run_view();
    batch_.items = std::move(items);
    batch_.selected = -1;
    batchStale_ = false;
    int ok = 0;
    for (const auto& it : batch_.items) ok += it.analysis.has_value();
    batch_.status = "Analysed " + std::to_string(batch_.items.size()) + " run(s): " + std::to_string(ok) + " fitted";
    if (ok < static_cast<int>(batch_.items.size()))
        batch_.status += ", " + std::to_string(batch_.items.size() - ok) + " failed";
    batch_.status += ".";
    for (size_t i = 0; i < batch_.items.size(); ++i)
        if (batch_.items[i].analysis) {
            show_batch_item(static_cast<int>(i));
            break;
        }
    if (batch_.writeReports) write_batch_reports();
}

void App::show_batch_item(int index) {
    if (index < 0 || index >= static_cast<int>(batch_.items.size())) return;
    // The table's own fit waits while the run is shown.
    if (!tableResult_) tableResult_ = TableResult{analysis_, analysisRun_};
    batch_.selected = index;
    const auto& item = batch_.items[static_cast<size_t>(index)];
    analysis_ = item.analysis;  // empty for a run that could not be fitted
    analysisRun_ = item.runNumber;
    import_.selectedRun = item.runNumber;
    reportWritten_ = false;
    refitPlot_ = true;
}

void App::leave_run_view() {
    if (!tableResult_) {
        batch_.selected = -1;
        return;
    }
    analysis_ = std::move(tableResult_->analysis);
    analysisRun_ = tableResult_->run;
    tableResult_.reset();
    batch_.selected = -1;
    reportWritten_ = false;
    refitPlot_ = true;
}

void App::refit_runs() {
    const int shown = batch_.selected;
    analyse_all_runs();
    if (shown >= 0 && shown < static_cast<int>(batch_.items.size())) show_batch_item(shown);
}

void App::edit_shown_run_in_table() {
    const auto* item = shown_item();
    if (!item) return;
    const auto analysis = item->analysis;
    const int run = item->runNumber;
    const bool stale = batchStale_;
    import_.selectedRun = run;
    if (!load_selected_run_into_table()) return;
    // The table now holds the run's points, so the run's fit is the table's.
    const auto undo = std::exchange(notice_, std::nullopt);
    if (analysis) {
        analysis_ = analysis;
        analysisRun_ = run;
        stale_ = stale;
        refitPlot_ = true;
    }
    notice_ = undo;
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

std::string App::current_report_text(bool forFile) const {
    if (!analysis_) return {};
    const bool first = forFile ? (report_.newFile || !std::filesystem::exists(report_.file) ||
                                  std::filesystem::file_size(report_.file) == 0)
                               : true;
    return kin::format_report(*analysis_, report_.options, report_context(analysisRun_, first));
}

void App::choose_report_file() {
    show_save_dialog(FilePurpose::ReportFile,
                     report_.file.empty() ? default_dir() + "results.txt" : report_.file.string());
}

bool App::append_report(const kin::Analysis& a, std::optional<int> runNumber) {
    std::error_code ec;
    const bool empty = !std::filesystem::exists(report_.file, ec) || std::filesystem::file_size(report_.file, ec) == 0;
    const auto text = kin::format_report(a, report_.options, report_context(runNumber, report_.newFile || empty));
    if (!kin::write_text_file(report_.file, text, !report_.newFile)) {
        error("Can't write the report file " + report_.file.filename().string() + ".", report_.file.string());
        return false;
    }
    report_.newFile = false;
    ++report_.nextNumber;
    return true;
}

void App::write_report() {
    if (!analysis_) {
        error("There are no results to report yet: fit the data first (F5).");
        return;
    }
    if (report_.file.empty()) {
        pendingReport_ = PendingReport::Current;
        choose_report_file();
        return;
    }
    const int number = report_.nextNumber;
    if (append_report(*analysis_, analysisRun_)) {
        report_.status = "Report" + (report_.options.numbering == kin::Numbering::Sequential ? " #" + std::to_string(number) : std::string()) +
                         " written to " + report_.file.filename().string() + ".";
        file_written(report_.file);
        reportWritten_ = true;
        info(report_.status, report_.file.string());  // seen from any tab (Alt+P)
    }
}

void App::write_batch_reports() {
    if (report_.file.empty()) {
        pendingReport_ = PendingReport::Batch;
        choose_report_file();
        return;
    }
    int written = 0;
    for (const auto& item : batch_.items) {
        if (!item.analysis) continue;
        if (!append_report(*item.analysis, item.runNumber)) return;
        ++written;
    }
    if (written > 0) file_written(report_.file);
    batch_.status += " " + std::to_string(written) + " report(s) written to " + report_.file.filename().string() + ".";
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
    if (analysisRun_) t += ", run #" + std::to_string(*analysisRun_);
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
    ImGui::SetClipboardText(t.c_str());
    info("Results copied to the clipboard, as a table for a spreadsheet.");
}

void App::clear_table() {
    leave_run_view();
    auto before = snapshot_table();
    if (before.rows.empty()) return;  // nothing to clear
    const size_t count = before.rows.size();
    set_table({});
    dataSource_ = "Entered by hand";
    dataRun_.reset();
    Notice n;
    n.text = "Cleared the table (" + std::to_string(count) + " row" + (count == 1 ? "" : "s") + ").";
    n.undo = std::move(before);
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
            case FilePurpose::ReportFile: {
                report_.file = path;
                std::error_code ec;
                report_.newFile = !std::filesystem::exists(path, ec);
                report_.nextNumber = report_.options.startNumber;
                save_settings();  // at once: a browser tab may be closed without a save
                const auto pending = pendingReport_;
                pendingReport_ = PendingReport::None;
                if (pending == PendingReport::Current) write_report();
                else if (pending == PendingReport::Batch) write_batch_reports();
                break;
            }
        }
    }
}

}  // namespace app
