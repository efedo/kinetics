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
    // Show the options folded only when they hold nothing unusual.
    openFitOptions_ = !fit_settings_are_default();
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
    import_.negativeIsMissing = s.get_bool("import.negativeIsMissing", true);
    import_.mpaCount = std::max(1, s.get_int("import.mpaCount", 8));
    if (auto t = theme_from_name(s.get("view.theme", "system"))) set_theme(*t);
    set_text_scale(s.get_int("view.textSize", 100));
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
    s.set_bool("import.negativeIsMissing", import_.negativeIsMissing);
    s.set_int("import.mpaCount", import_.mpaCount);
    s.set("view.theme", theme_name(theme_choice()));
    s.set_int("view.textSize", textScale_);
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
    if (analysis_) mark_stale();
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
    // While a run from the file analysis is shown, the table's state does not
    // apply to it.
    return analysis_ && stale_ && !(batch_.selected >= 0 && analysisRun_);
}

void App::close_import() {
    const bool showingRun = batch_.selected >= 0 && analysisRun_.has_value();
    ImportState fresh;
    fresh.negativeIsMissing = import_.negativeIsMissing;  // preferences, not the file
    fresh.mpaCount = import_.mpaCount;
    import_ = std::move(fresh);
    batch_ = BatchState{};
    // Results of one of the file's runs go with the file.
    if (showingRun) {
        analysis_.reset();
        analysisRun_.reset();
        stale_ = false;
    }
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

std::string App::fit_settings_summary() const {
    const auto& def = kin::model_def(model_);
    const bool linear = def.family == kin::Family::Linear;
    const auto defaults = kin::default_param_settings(model_);
    const auto iv0 = kin::param_index(model_, kin::Role::V0);
    std::string fixed;
    bool customBounds = false;
    for (size_t j = 0; j < params_.size() && j < def.params.size(); ++j) {
        const auto& s = params_[j];
        if (j < defaults.size() && (s.min != defaults[j].min || s.max != defaults[j].max)) customBounds = true;
        if (!s.fixed) continue;
        fixed += fixed.empty() ? "" : ", ";
        fixed += std::string(def.params[j].name) + " fixed ";
        fixed += (iv0 && *iv0 == static_cast<int>(j) && v0_measured()) ? "at the measured value"
                                                                        : "at " + kin::fmt_g(s.value, 4);
    }
    std::string s = fixed.empty() ? "All parameters fitted" : fixed;
    if (linear) return s + "; linear regression, solved directly.";
    if (customBounds) s += ", custom bounds";
    s += method_ == kin::Method::GaussNewton ? "; Gauss-Newton" : "; Marquardt-Levenberg";
    s += guess_ == kin::GuessMode::Manual ? ", manual estimates" : ", automatic estimates";
    if (maxIterations_ > 0) s += ", at most " + std::to_string(maxIterations_) + " iterations";
    return s + ".";
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
    rows_.clear();
    for (const auto& p : pts) rows_.push_back({number_text(p.x), number_text(p.y)});
    rows_.push_back({});
    mark_stale();
}

void App::set_table_y(const std::vector<double>& ys) {
    // Keep existing X values (MPA files carry velocities only).
    while (!rows_.empty() && rows_.back().x.empty() && rows_.back().y.empty()) rows_.pop_back();
    if (rows_.size() < ys.size()) rows_.resize(ys.size());
    for (size_t i = 0; i < rows_.size(); ++i) rows_[i].y = i < ys.size() ? number_text(ys[i]) : std::string();
    rows_.push_back({});
    mark_stale();
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
        error("Can't open " + path.string());
        return;
    }
    const auto pts = kin::parse_xy_table(*text);
    if (pts.empty()) {
        error(path.filename().string() + " does not contain two columns of numbers.");
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
        break;
    }
}

void App::save_table(const std::filesystem::path& path) {
    const auto& def = kin::model_def(model_);
    if (!kin::write_text_file(path, kin::format_xy_table(table_points(), def.xName, def.yName), false)) {
        error("Can't write " + path.string());
        return;
    }
    tablePath_ = path;
    dataSource_ = path.filename().string();
    file_written(path);
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
    apply_analysis(kin::analyse(make_request(table_points())), dataRun_);
    batch_.selected = -1;
}

void App::apply_analysis(kin::AnalysisOutcome outcome, std::optional<int> runNumber) {
    if (!outcome.analysis) {
        error(outcome.error);
        return;
    }
    analysis_ = std::move(outcome.analysis);
    analysisRun_ = runNumber;
    stale_ = false;
    refitPlot_ = true;
    const auto plots = kin::available_plots(analysis_->model());
    if (std::find(plots.begin(), plots.end(), plotType_) == plots.end()) set_plot_type(kin::PlotType::Normal);
    // A fit that did not converge may need other options: show them.
    if (!analysis_->linear && analysis_->fit.status != kin::FitStatus::Converged) openFitOptions_ = true;
}

// ===========================================================================
// File runs
// ===========================================================================

void App::import_spreadsheet(const std::filesystem::path& path) {
    const auto text = kin::read_text_file(path);
    if (!text) {
        error("Can't open " + path.string());
        return;
    }
    auto parsed = kin::parse_spreadsheet(*text);
    if (!parsed.file) {
        error(path.filename().string() + ": " + parsed.error);
        return;
    }
    import_.type = ImportType::Spreadsheet;
    import_.path = path;
    import_.sheet = std::move(parsed.file);
    import_.mpaText.clear();
    import_.selectedRun = import_.sheet->runs.front().number;
    batch_ = BatchState{};
    batch_.firstRun = import_.selectedRun;
    requestedTab_ = Tab::FileRuns;
}

void App::import_mpa(const std::filesystem::path& path) {
    const auto text = kin::read_text_file(path);
    if (!text) {
        error("Can't open " + path.string());
        return;
    }
    import_.type = ImportType::Mpa;
    import_.path = path;
    import_.sheet.reset();
    import_.mpaText = *text;
    import_.selectedRun = 1;
    batch_ = BatchState{};
    requestedTab_ = Tab::FileRuns;
}

bool App::load_selected_run_into_table() {
    const auto name = import_.path.filename().string();
    if (import_.type == ImportType::Spreadsheet) {
        const auto* run = import_.sheet->find_run(import_.selectedRun);
        if (!run) {
            error("Run #" + std::to_string(import_.selectedRun) + " not found in " + name + ".");
            return false;
        }
        set_table(import_.sheet->points(*run, import_.negativeIsMissing));
        dataSource_ = "Run #" + std::to_string(run->number) + " of " + name;
        dataRun_ = run->number;
        return true;
    }
    if (import_.type == ImportType::Mpa) {
        const auto ds = kin::parse_mpa_dataset(import_.mpaText, import_.selectedRun, import_.mpaCount);
        if (!ds.error.empty()) {
            error(name + ": " + ds.error);
            return false;
        }
        set_table_y(ds.y);
        dataSource_ = "Dataset #" + std::to_string(import_.selectedRun) + " of " + name + " (velocities)";
        dataRun_ = import_.selectedRun;
        return true;
    }
    error("No file is imported. Use File > Import spreadsheet file of runs, or Import plate-reader file.");
    return false;
}

void App::fit_selected_run() {
    if (load_selected_run_into_table()) request_fit();
}

void App::analyse_all_runs() {
    const auto tmpl = make_request({});
    if (import_.type == ImportType::Spreadsheet) {
        batch_.items = kin::analyse_spreadsheet_runs(*import_.sheet, batch_.firstRun, tmpl, import_.negativeIsMissing);
        if (batch_.items.empty()) {
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
        batch_.items = kin::analyse_mpa_datasets(import_.mpaText, batch_.firstRun, import_.mpaCount, xs, tmpl);
        if (batch_.items.empty()) {
            error("Dataset #" + std::to_string(batch_.firstRun) + " not found in " + import_.path.filename().string() + ".");
            return;
        }
    } else {
        error("No file is imported. Use File > Import spreadsheet file of runs, or Import plate-reader file.");
        return;
    }
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
    batch_.selected = index;
    const auto& item = batch_.items[static_cast<size_t>(index)];
    if (!item.analysis) return;
    analysis_ = item.analysis;
    analysisRun_ = item.runNumber;
    stale_ = false;
    refitPlot_ = true;
}

// ===========================================================================
// Reports
// ===========================================================================

std::string App::current_report_text(bool forFile) const {
    if (!analysis_) return {};
    kin::ReportContext ctx;
    ctx.sequenceNumber = report_.nextNumber;
    ctx.runNumber = analysisRun_;
    ctx.today = kin::today_string();
    ctx.firstInFile = forFile ? (report_.newFile || !std::filesystem::exists(report_.file) ||
                                 std::filesystem::file_size(report_.file) == 0)
                              : true;
    return kin::format_report(*analysis_, report_.options, ctx);
}

void App::choose_report_file() {
    show_save_dialog(FilePurpose::ReportFile,
                     report_.file.empty() ? default_dir() + "results.txt" : report_.file.string());
}

bool App::append_report(const kin::Analysis& a, std::optional<int> runNumber) {
    std::error_code ec;
    const bool empty = !std::filesystem::exists(report_.file, ec) || std::filesystem::file_size(report_.file, ec) == 0;
    kin::ReportContext ctx;
    ctx.sequenceNumber = report_.nextNumber;
    ctx.runNumber = runNumber;
    ctx.today = kin::today_string();
    ctx.firstInFile = report_.newFile || empty;
    const auto text = kin::format_report(a, report_.options, ctx);
    if (!kin::write_text_file(report_.file, text, !report_.newFile)) {
        error("Can't write the report file " + report_.file.string());
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
}

// ===========================================================================
// Dialog results, errors
// ===========================================================================

void App::error(const std::string& message) {
    errorText_ = message;
    openError_ = true;
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
                const auto pending = pendingReport_;
                pendingReport_ = PendingReport::None;
                if (pending == PendingReport::Current) write_report();
                else if (pending == PendingReport::Batch) write_batch_reports();
                break;
            }
        }
    }
}

// ===========================================================================
// Frame
// ===========================================================================

bool App::frame() {
    handle_file_results();
    handle_shortcuts();

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_MenuBar |
                                   ImGuiWindowFlags_NoBringToFrontOnFocus;
    ImGui::Begin("Kinetics##main", nullptr, flags);
    ImGui::PopStyleVar(2);

    draw_menu_bar();
    draw_header();

    if (ImGui::BeginTable("##layout", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV,
                          ImGui::GetContentRegionAvail())) {
        ImGui::TableSetupColumn("left", ImGuiTableColumnFlags_WidthStretch, 0.38f);
        ImGui::TableSetupColumn("right", ImGuiTableColumnFlags_WidthStretch, 0.62f);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        if (ImGui::BeginChild("##left", ImVec2(0, 0), ImGuiChildFlags_None)) draw_left_panel();
        ImGui::EndChild();
        ImGui::TableNextColumn();
        if (ImGui::BeginChild("##right", ImVec2(0, 0), ImGuiChildFlags_None)) draw_right_panel();
        ImGui::EndChild();
        ImGui::EndTable();
    }
    ImGui::End();

    draw_dialogs();
    draw_help_window();
    return !quit_;
}

void App::draw_header() {
    const auto desc = plain_model_label(model_, params_);
    int invalid = 0;
    const auto pts = kin::clean_data(table_points(&invalid));
    ImGui::TextUnformatted(desc.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    ImGui::Text("Data: %s, %zu point%s", dataSource_.c_str(), pts.size(), pts.size() == 1 ? "" : "s");
    if (invalid > 0) {
        ImGui::SameLine();
        ImGui::TextColored(palette().warning, "(%d cell%s not a number)", invalid, invalid == 1 ? "" : "s");
    }
    if (import_.type != ImportType::None) {
        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
        ImGui::Text("File: %s", import_.path.filename().string().c_str());
    }
    ImGui::Separator();
}

void App::draw_menu_bar() {
    if (!ImGui::BeginMenuBar()) return;
    const bool haveFit = analysis_.has_value();
    const bool haveImport = import_.type != ImportType::None;

    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("Open data table...", "Ctrl+O")) show_open_dialog(FilePurpose::OpenTable, default_dir());
        if (ImGui::MenuItem("Save data table...", "Ctrl+S"))
            show_save_dialog(FilePurpose::SaveTable, tablePath_.empty() ? default_dir() + "data.csv" : tablePath_.string());
        ImGui::Separator();
        if (ImGui::MenuItem("Import spreadsheet file of runs..."))
            show_open_dialog(FilePurpose::ImportSpreadsheet, default_dir());
        ImGui::SetItemTooltip("Concentrations, then one row of velocities per run");
        if (ImGui::MenuItem("Import plate-reader file (MPA, velocities only)..."))
            show_open_dialog(FilePurpose::ImportMpa, default_dir());
        if (ImGui::MenuItem("Close imported file", nullptr, false, haveImport)) close_import();
        if (ImGui::BeginMenu("Open example")) {
            if (ImGui::MenuItem("Michaelis-Menten data table")) open_example(Example::MichaelisMenten);
            if (ImGui::MenuItem("Arrhenius data table")) open_example(Example::Arrhenius);
            if (ImGui::MenuItem("Spreadsheet file with four runs (Hill)")) open_example(Example::SpreadsheetRuns);
            if (ImGui::MenuItem("MPA file with two datasets (inhibition)")) open_example(Example::MpaPlate);
            ImGui::EndMenu();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Report options...", "Alt+O")) openReportOptions_ = true;
        if (ImGui::MenuItem("Write report", "Alt+P", false, haveFit)) write_report();
        if (ImGui::MenuItem("Copy report to clipboard", nullptr, false, haveFit)) copy_report();
        if (!kIsWeb) {  // a browser tab is closed by the browser
            ImGui::Separator();
            if (ImGui::MenuItem("Quit", "Alt+Q")) quit_ = true;
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit")) {
        if (ImGui::BeginMenu("Equation", true)) {
            for (const auto& m : kin::all_models()) {
                const std::string label = std::string(1, m.letter) + ". " + std::string(m.menuName);
                if (ImGui::MenuItem(label.c_str(), nullptr, m.id == model_) && m.id != model_) set_model(m.id);
            }
            ImGui::EndMenu();
        }
        ImGui::SetItemTooltip("Alt+T");
        if (ImGui::MenuItem("Enter/edit data", "Alt+D")) focusTable_ = true;
        if (ImGui::MenuItem("Load imported run into table", "Alt+M", false, haveImport)) load_selected_run_into_table();
        ImGui::Separator();
        if (ImGui::MenuItem("Paste data from clipboard")) paste_table_from_clipboard();
        if (ImGui::MenuItem("Sort data")) {
            auto pts = table_points();
            std::stable_sort(pts.begin(), pts.end(), [](const kin::Point& a, const kin::Point& b) {
                if (std::isnan(a.x)) return false;
                if (std::isnan(b.x)) return true;
                return a.x < b.x || (a.x == b.x && a.y < b.y);
            });
            set_table(pts);
        }
        if (ImGui::MenuItem("Clear data")) {
            set_table({});
            dataSource_ = "Entered by hand";
            dataRun_.reset();
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Calculate")) {
        const auto& def = kin::model_def(analysis_ ? analysis_->model() : model_);
        const std::string xName(def.xName), yName(def.yName);
        if (ImGui::MenuItem("Fit table data", "F5 / Alt+R")) request_fit();
        if (ImGui::MenuItem("Fit the selected file run", "Alt+S", false, haveImport)) fit_selected_run();
        if (ImGui::MenuItem("Fit all file runs", "Alt+A", false, haveImport)) {
            requestedTab_ = Tab::FileRuns;
            analyse_all_runs();
        }
        if (ImGui::MenuItem("Show file runs", "Alt+W", false, haveImport)) requestedTab_ = Tab::FileRuns;
        ImGui::Separator();
        if (ImGui::MenuItem(("Find " + xName + " for a given " + yName + "...").c_str(), "Alt+X", false, haveFit)) {
            requestedTab_ = Tab::StandardCurve;
            focusStdX_ = true;
        }
        if (ImGui::MenuItem(("Find " + yName + " for a given " + xName + "...").c_str(), "Alt+Y", false, haveFit)) {
            requestedTab_ = Tab::StandardCurve;
            focusStdY_ = true;
        }
        ImGui::Separator();
        ImGui::TextDisabled("Initial estimates");
        if (ImGui::MenuItem("Automatic", nullptr, guess_ == kin::GuessMode::Automatic)) {
            guess_ = kin::GuessMode::Automatic;
            mark_stale();
        }
        if (ImGui::MenuItem("Manual (values in the parameter table)", nullptr, guess_ == kin::GuessMode::Manual)) {
            guess_ = kin::GuessMode::Manual;
            mark_stale();
            openFitOptions_ = true;  // the estimates are entered there
        }
        ImGui::TextDisabled("Fitting method");
        if (ImGui::MenuItem("Marquardt-Levenberg", nullptr, method_ == kin::Method::Marquardt)) {
            method_ = kin::Method::Marquardt;
            mark_stale();
        }
        if (ImGui::MenuItem("Gauss-Newton", nullptr, method_ == kin::Method::GaussNewton)) {
            method_ = kin::Method::GaussNewton;
            mark_stale();
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
        // The tabs other than Results appear once they have something to show.
        if (ImGui::MenuItem("Results", "Alt+L")) requestedTab_ = Tab::Results;
        if (ImGui::MenuItem("Graph", "Alt+G", false, haveFit)) requestedTab_ = Tab::Graph;
        if (ImGui::MenuItem("Residuals", "Alt+I", false, haveFit)) requestedTab_ = Tab::Residuals;
        if (ImGui::MenuItem("Standard curve", nullptr, false, haveFit)) requestedTab_ = Tab::StandardCurve;
        if (ImGui::MenuItem("File runs", nullptr, false, haveImport)) requestedTab_ = Tab::FileRuns;
        if (ImGui::MenuItem("Report", nullptr, false, haveFit)) requestedTab_ = Tab::Report;
        ImGui::Separator();
        if (ImGui::MenuItem("Classic interface (v3.51)", "Ctrl+F12")) classicRequested_ = true;
        if (ImGui::BeginMenu("Text size")) {
            if (ImGui::MenuItem("Larger", "Ctrl+=", false, textScale_ < kTextScales.back())) step_text_scale(+1);
            if (ImGui::MenuItem("Smaller", "Ctrl+-", false, textScale_ > kTextScales.front())) step_text_scale(-1);
            ImGui::Separator();
            for (int pct : kTextScales) {
                const std::string label = std::to_string(pct) + "%";
                if (ImGui::MenuItem(label.c_str(), pct == 100 ? "Ctrl+0" : nullptr, textScale_ == pct) && pct != textScale_) {
                    set_text_scale(pct);
                    save_settings();
                }
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Theme")) {
            const ThemeChoice now = theme_choice();
            for (const auto& [choice, label] : {std::pair{ThemeChoice::Dark, "Dark"},
                                                std::pair{ThemeChoice::Light, "Light"},
                                                std::pair{ThemeChoice::System, "Same as the system"}})
                if (ImGui::MenuItem(label, nullptr, now == choice) && choice != now) {
                    set_theme(choice);
                    save_settings();
                }
            ImGui::EndMenu();
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Help")) {
        const auto topics = help_topics();
        for (int i = 0; i < static_cast<int>(topics.size()); ++i)
            if (ImGui::MenuItem(topics[static_cast<size_t>(i)].title, i == 0 ? "Alt+H / F1" : nullptr)) {
                showHelp_ = true;
                helpTopic_ = i;
            }
        ImGui::Separator();
        if (ImGui::MenuItem("About Kinetics")) openAbout_ = true;
        ImGui::EndMenu();
    }
    ImGui::EndMenuBar();
}

void App::handle_shortcuts() {
    using namespace ImGui;
    constexpr ImGuiInputFlags g = ImGuiInputFlags_RouteGlobal;
    const bool haveImport = import_.type != ImportType::None;
    if (Shortcut(ImGuiMod_Alt | ImGuiKey_Q, g) && !kIsWeb) quit_ = true;
    if (Shortcut(ImGuiMod_Alt | ImGuiKey_O, g)) openReportOptions_ = true;
    if (Shortcut(ImGuiMod_Alt | ImGuiKey_P, g)) write_report();
    if (Shortcut(ImGuiMod_Alt | ImGuiKey_T, g)) focusModel_ = true;
    if (Shortcut(ImGuiMod_Alt | ImGuiKey_D, g)) focusTable_ = true;
    if (Shortcut(ImGuiMod_Alt | ImGuiKey_M, g)) load_selected_run_into_table();
    if (Shortcut(ImGuiMod_Alt | ImGuiKey_R, g) || Shortcut(ImGuiKey_F5, g)) request_fit();
    if (Shortcut(ImGuiMod_Alt | ImGuiKey_S, g)) fit_selected_run();
    if (Shortcut(ImGuiMod_Alt | ImGuiKey_A, g) && haveImport) {
        requestedTab_ = Tab::FileRuns;
        analyse_all_runs();
    }
    if (Shortcut(ImGuiMod_Alt | ImGuiKey_W, g)) requestedTab_ = Tab::FileRuns;
    if (Shortcut(ImGuiMod_Alt | ImGuiKey_X, g)) {
        requestedTab_ = Tab::StandardCurve;
        focusStdX_ = true;
    }
    if (Shortcut(ImGuiMod_Alt | ImGuiKey_Y, g)) {
        requestedTab_ = Tab::StandardCurve;
        focusStdY_ = true;
    }
    if (Shortcut(ImGuiMod_Alt | ImGuiKey_L, g)) requestedTab_ = Tab::Results;
    if (Shortcut(ImGuiMod_Alt | ImGuiKey_G, g)) requestedTab_ = Tab::Graph;
    if (Shortcut(ImGuiMod_Alt | ImGuiKey_I, g)) requestedTab_ = Tab::Residuals;
    if (Shortcut(ImGuiMod_Alt | ImGuiKey_H, g) || Shortcut(ImGuiKey_F1, g)) showHelp_ = true;
    if (Shortcut(ImGuiMod_Ctrl | ImGuiKey_O, g)) show_open_dialog(FilePurpose::OpenTable, default_dir());
    if (Shortcut(ImGuiMod_Ctrl | ImGuiKey_S, g))
        show_save_dialog(FilePurpose::SaveTable, tablePath_.empty() ? default_dir() + "data.csv" : tablePath_.string());
    // Text size: Ctrl+= (or Ctrl++), Ctrl+-, Ctrl+0, also on the keypad.
    if (Shortcut(ImGuiMod_Ctrl | ImGuiKey_Equal, g) || Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Equal, g) ||
        Shortcut(ImGuiMod_Ctrl | ImGuiKey_KeypadAdd, g))
        step_text_scale(+1);
    if (Shortcut(ImGuiMod_Ctrl | ImGuiKey_Minus, g) || Shortcut(ImGuiMod_Ctrl | ImGuiKey_KeypadSubtract, g))
        step_text_scale(-1);
    if ((Shortcut(ImGuiMod_Ctrl | ImGuiKey_0, g) || Shortcut(ImGuiMod_Ctrl | ImGuiKey_Keypad0, g)) && textScale_ != 100) {
        set_text_scale(100);
        save_settings();
    }
}

}  // namespace app
