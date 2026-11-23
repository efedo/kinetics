// The graph on the Results tab (v3.51 View > Graph and PLOT.CPP), drawn with
// ImPlot: the plot list and display options, the plot, and on the normal
// plot a strip of residuals under it that shares its X axis.

#include <imgui.h>
#include <implot.h>

#include <algorithm>
#include <cfloat>
#include <cstdio>

#include "app/app.hpp"
#include "app/theme.hpp"
#include "app/ui_common.hpp"

namespace app {
namespace {

// A one-letter axis name reads badly, and rotated on the Y axis "v" looks
// like ">", so the plain axes get a word; a unit follows it.
std::string axis_label(const std::string& name, const std::string& unit) {
    std::string word;
    if (name == "v") word = "velocity";
    else if (name == "[S]") word = "substrate";
    else if (name == "[A]") word = "activator";
    else if (name == "[I]") word = "inhibitor";
    if (word.empty()) return unit.empty() ? name : name + " (" + unit + ")";
    return name + " (" + word + (unit.empty() ? "" : ", " + unit) + ")";
}

// The plot list in three groups: the plots to judge the fit by, the linear
// transforms, and the rest.
struct PlotGroup {
    const char* title;
    std::vector<kin::PlotType> types;
};

const std::vector<PlotGroup>& plot_groups() {
    using P = kin::PlotType;
    static const std::vector<PlotGroup> groups = {
        {"Recommended", {P::Normal, P::Residual}},
        {"Linear transforms (for display only)", {P::LineweaverBurk, P::HanesWoolf, P::EadieHofstee}},
        {"Other plots", {P::Hill, P::LogLog, P::Saturation, P::Job, P::Arrhenius}},
    };
    return groups;
}

// "Km = 2.71 ± 0.14 µM, Vmax = 10.24 ± 0.18" for the legend.
std::string fitted_values(const kin::Analysis& a, const std::vector<std::string>& units) {
    std::string s;
    const auto rows = a.parameter_rows();
    for (size_t i = 0; i < rows.size(); ++i) {
        const auto& r = rows[i];
        if (r.derived) continue;
        s += (s.empty() ? "" : ", ") + r.name + " = " +
             (r.fixed ? format_value_sd(r.value, NAN).value + " (fixed)" : format_value_pm(r.value, r.sd));
        if (i < units.size() && !units[i].empty()) s += " " + units[i];
    }
    return s;
}

}  // namespace

void App::draw_plot_controls() {
    if (!analysis_) return;
    const auto m = analysis_->model();
    const auto plots = kin::available_plots(m);
    const bool residual = plotType_ == kin::PlotType::Residual;
    // The X intercept says something only on the straight-line plots.
    const bool interceptMeaningful = !residual && (plotType_ != kin::PlotType::Normal || m == kin::ModelId::Linear);

    field_label("Plot");
    ImGui::SetNextItemWidth(std::min(ImGui::GetFontSize() * 18.0f, ImGui::GetContentRegionAvail().x * 0.45f));
    if (ImGui::BeginCombo("##plottype", kin::plot_name(plotType_, m).c_str(), ImGuiComboFlags_HeightLarge)) {
        for (const auto& g : plot_groups()) {
            std::vector<kin::PlotType> here;
            for (auto t : g.types)
                if (std::find(plots.begin(), plots.end(), t) != plots.end()) here.push_back(t);
            if (here.empty()) continue;
            ImGui::SeparatorText(g.title);
            const bool transform = g.types.front() == kin::PlotType::LineweaverBurk;
            for (auto t : here) {
                if (ImGui::Selectable(kin::plot_name(t, m).c_str(), t == plotType_) && t != plotType_) set_plot_type(t);
                if (transform)
                    ImGui::SetItemTooltip("These plots distort the experimental errors. Read the constants from the fit "
                                          "(above), not from the line.");
            }
        }
        ImGui::EndCombo();
    }

    // Display options, showing only those that apply to this plot. In a
    // narrow panel the buttons go onto the next line.
    const float helpW = ImGui::CalcTextSize("(?)").x + ImGui::GetStyle().ItemSpacing.x * 2.0f;
    const float displayW = ImGui::CalcTextSize("Display").x + ImGui::GetFrameHeight() + ImGui::GetStyle().FramePadding.x * 2.0f;
    same_line_if_fits(displayW + helpW);
    bool changed = false;
    ImGui::SetNextItemWidth(displayW);
    if (ImGui::BeginCombo("##display", "Display", ImGuiComboFlags_HeightLarge)) {
        changed |= ImGui::Checkbox("Include the origin", &plotOptions_.includeOrigin);
        if (!residual) changed |= ImGui::Checkbox("Extend the curve to the Y axis", &plotOptions_.extendToYAxis);
        if (interceptMeaningful) {
            changed |= ImGui::Checkbox("Extend the line to the X intercept", &plotOptions_.extendToXIntercept);
            ImGui::SetItemTooltip("Draws the line to where it crosses Y = 0 and labels that X value");
        }
        bool save = ImGui::Checkbox("Number the points", &numberPoints_);
        if (!residual) save |= ImGui::Checkbox("Show the fitted values", &showFittedValues_);
        if (plotType_ == kin::PlotType::Normal) {
            if (ImGui::Checkbox("Show the residuals under the graph", &residualStrip_)) {
                save = true;
                changed = true;
            }
            ImGui::SetItemTooltip("Each point's distance from the curve, against the same X axis. They should "
                                  "scatter on both sides of zero with no pattern.");
        }
        if (save) save_settings();
        ImGui::EndCombo();
    }
    same_line_if_fits(button_width("Reset view") + helpW);
    if (ImGui::Button("Reset view")) refitPlot_ = true;
    ImGui::SetItemTooltip("Fit the axes to the data again after zooming or panning");
    if (changed) refitPlot_ = true;
    help_link(HelpId::Plotting);
}

void App::draw_plot(const ImVec2& size) {
    if (!analysis_) return;
    const auto& a = *analysis_;
    const auto m = a.model();
    const auto& def = kin::model_def(m);
    const bool residual = plotType_ == kin::PlotType::Residual;
    const bool interceptMeaningful = !residual && (plotType_ != kin::PlotType::Normal || m == kin::ModelId::Linear);
    // The residual strip needs some height to be readable: about a third of
    // the plot and at least 6 font heights. Without room for it and the
    // curve, it is left out.
    const float fs = ImGui::GetFontSize();
    const float stripH = std::clamp(size.y * 0.3f, fs * 6.0f, fs * 10.0f);
    const bool strip = residualStrip_ && plotType_ == kin::PlotType::Normal && !a.data.empty() &&
                       size.y - stripH >= fs * 9.0f;
    const auto pd = kin::make_plot(a, plotType_, plotOptions_);

    // Units for the axes of the normal plot, whose axes are the data's.
    std::vector<std::string> units;
    for (const auto& p : def.params) units.push_back(unit_of(p.role, m));
    const bool normal = plotType_ == kin::PlotType::Normal;
    const bool arrhenius = def.family == kin::Family::Arrhenius;
    const std::string xUnit = normal && !arrhenius ? xUnit_ : std::string();
    const std::string yUnit = normal && !arrhenius ? yUnit_ : std::string();
    const std::string xLabel = axis_label(pd.xLabel, xUnit), yLabel = axis_label(pd.yLabel, yUnit);

    // ImPlot ignores ImGui's global alpha, so out-of-date results are greyed
    // out here by fading the plot's own colours.
    const float fade = results_stale() ? kStaleAlpha : 1.0f;
    auto faded = [fade](ImVec4 c) { return ImVec4(c.x, c.y, c.z, c.w * fade); };
    int pushed = 0;
    if (fade < 1.0f) {
        for (ImPlotCol col : {ImPlotCol_AxisText, ImPlotCol_AxisGrid, ImPlotCol_AxisTick, ImPlotCol_LegendText,
                              ImPlotCol_PlotBorder, ImPlotCol_InlayText}) {
            ImPlot::PushStyleColor(col, faded(ImPlot::GetStyle().Colors[col]));
            ++pushed;
        }
    }
    const bool refit = std::exchange(refitPlot_, false);

    // Axes through the origin, as in v3.51's plots.
    auto origin_axes = [&](bool vertical) {
        ImPlotSpec axis;
        const ImVec4 dim = palette().textDim;
        axis.LineColor = ImVec4(dim.x, dim.y, dim.z, 0.6f * fade);
        axis.Flags = ImPlotItemFlags_NoLegend | ImPlotItemFlags_NoFit;
        const double zero = 0.0;
        if (vertical) ImPlot::PlotInfLines("##x0", &zero, 1, axis);
        axis.Flags |= ImPlotInfLinesFlags_Horizontal;
        ImPlot::PlotInfLines("##y0", &zero, 1, axis);
    };

    auto main_plot = [&](const ImVec2& plotSize) {
        if (!ImPlot::BeginPlot("##graph", plotSize, ImPlotFlags_NoTitle)) return;
        // With the residual strip below, its X axis carries the labels.
        ImPlot::SetupAxes(strip ? nullptr : xLabel.c_str(), yLabel.c_str(), strip ? ImPlotAxisFlags_NoTickLabels : 0, 0);
        if (refit) ImPlot::SetupAxesLimits(pd.xmin, pd.xmax, pd.ymin, pd.ymax, ImPlotCond_Always);
        ImPlot::SetupLegend(ImPlotLocation_North, ImPlotLegendFlags_Outside | ImPlotLegendFlags_Horizontal);
        origin_axes(true);

        if (!pd.cx.empty()) {
            ImPlotSpec curve;
            curve.LineColor = faded(palette().accent);
            curve.LineWeight = 2.0f;
            // The legend carries the fitted constants, so that a copy of the
            // plot shows them; a plot too narrow for them keeps a short label.
            std::string label = showFittedValues_ ? "Fit: " + fitted_values(a, units) : std::string("Fitted curve");
            if (ImGui::CalcTextSize(label.c_str()).x + fs * 6.0f > ImPlot::GetPlotSize().x) label = "Fitted curve";
            ImPlot::PlotLine(label.c_str(), pd.cx.data(), pd.cy.data(), static_cast<int>(pd.cx.size()), curve);
        }

        ImPlotSpec pts;
        pts.Marker = ImPlotMarker_Circle;
        pts.MarkerSize = 4.5f;
        pts.LineColor = faded(palette().text);
        pts.MarkerFillColor = faded(palette().text);
        ImPlot::PlotScatter("Data", pd.px.data(), pd.py.data(), static_cast<int>(pd.px.size()), pts);

        if (numberPoints_) {
            ImPlotSpec label;
            label.LineColor = faded(palette().textDim);
            for (size_t i = 0; i < pd.px.size(); ++i) {
                if (!std::isfinite(pd.px[i]) || !std::isfinite(pd.py[i])) continue;
                char buf[16];
                std::snprintf(buf, sizeof buf, "%d", pd.labels[i]);
                ImPlot::PlotText(buf, pd.px[i], pd.py[i], ImVec2(9.0f, -9.0f), label);
            }
        }

        if (interceptMeaningful && plotOptions_.extendToXIntercept && pd.xIntercept) {
            ImPlot::Annotation(*pd.xIntercept, 0.0, faded(palette().accent), ImVec2(0, 18), true,
                               "X = %s", kin::fmt_g(*pd.xIntercept, 4).c_str());
        }
        ImPlot::EndPlot();
    };

    // The residuals against the same X: a good fit scatters them about zero.
    auto residual_strip = [&]() {
        if (!ImPlot::BeginPlot("##residstrip", ImVec2(0, 0), ImPlotFlags_NoTitle | ImPlotFlags_NoLegend)) return;
        const std::string residualLabel = yUnit.empty() ? std::string("Residual") : "Residual (" + yUnit + ")";
        ImPlot::SetupAxes(xLabel.c_str(), residualLabel.c_str());
        if (refit) {
            double r = 2.5 * (std::isfinite(a.fit.stdDev) ? a.fit.stdDev : 0.0);
            for (double e : a.resid)
                if (std::isfinite(e)) r = std::max(r, std::fabs(e) * 1.25);
            if (!(r > 0.0)) r = 1.0;
            ImPlot::SetupAxesLimits(pd.xmin, pd.xmax, -r, r, ImPlotCond_Always);
        }
        origin_axes(false);
        std::vector<double> xs, ys, bx, by;
        for (size_t i = 0; i < a.data.size() && i < a.resid.size(); ++i) {
            const bool big = i < a.largeResidual.size() && a.largeResidual[i];
            (big ? bx : xs).push_back(a.data[i].x);
            (big ? by : ys).push_back(a.resid[i]);
        }
        ImPlotSpec pts;
        pts.Marker = ImPlotMarker_Circle;
        pts.MarkerSize = 4.0f;
        pts.LineColor = faded(palette().text);
        pts.MarkerFillColor = faded(palette().text);
        ImPlot::PlotScatter("Residual", xs.data(), ys.data(), static_cast<int>(xs.size()), pts);
        // Residuals of 2 SDs or more, in amber as in the residual table.
        pts.LineColor = faded(palette().warning);
        pts.MarkerFillColor = faded(palette().warning);
        ImPlot::PlotScatter("Large residual", bx.data(), by.data(), static_cast<int>(bx.size()), pts);
        ImPlot::EndPlot();
    };

    if (strip) {
        float ratios[2] = {size.y - stripH, stripH};
        if (ImPlot::BeginSubplots("##plots", 2, 1, size,
                                  ImPlotSubplotFlags_NoTitle | ImPlotSubplotFlags_LinkCols | ImPlotSubplotFlags_NoResize,
                                  ratios)) {
            main_plot(ImVec2(0, 0));
            residual_strip();
            ImPlot::EndSubplots();
        }
    } else {
        main_plot(size);
    }
    ImPlot::PopStyleColor(pushed);
}

}  // namespace app
