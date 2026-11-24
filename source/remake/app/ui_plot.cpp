// The graph on the Results tab (v3.51 View > Graph and PLOT.CPP), drawn with
// ImPlot: the plot list and display options, the plot, and on the normal
// plot a strip of residuals under it that shares its X axis.

#include <imgui.h>
#include <implot.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
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

// The data point nearest the mouse on the current plot, within a little
// under a font height, or -1. xs and ys are in plot coordinates.
int point_under_mouse(const std::vector<double>& xs, const std::vector<double>& ys) {
    if (!ImPlot::IsPlotHovered()) return -1;
    const ImVec2 mouse = ImGui::GetMousePos();
    const float reach = ImGui::GetFontSize() * 0.7f;
    float bestD = reach * reach;
    int best = -1;
    for (size_t k = 0; k < xs.size() && k < ys.size(); ++k) {
        if (!std::isfinite(xs[k]) || !std::isfinite(ys[k])) continue;
        const ImVec2 q = ImPlot::PlotToPixels(xs[k], ys[k]);
        const float dx = q.x - mouse.x, dy = q.y - mouse.y;
        if (dx * dx + dy * dy <= bestD) {
            bestD = dx * dx + dy * dy;
            best = static_cast<int>(k);
        }
    }
    return best;
}

// A ring around the point at (x, y), in plot coordinates.
void ring(double x, double y) {
    ImPlot::GetPlotDrawList()->AddCircle(ImPlot::PlotToPixels(x, y), ImGui::GetFontSize() * 0.55f,
                                         ImGui::GetColorU32(palette().accent), 0, 2.0f);
}

}  // namespace

// The exact values of data point i, for the tooltip over it on the graph.
void App::point_tooltip(const kin::Analysis& a, int i) {
    const auto k = static_cast<size_t>(i);
    if (k >= a.data.size()) return;
    const auto& def = kin::model_def(a.model());
    const bool arrhenius = def.family == kin::Family::Arrhenius;
    const std::string xu = xUnit_.empty() || arrhenius ? std::string() : " " + xUnit_;
    const std::string yu = yUnit_.empty() || arrhenius ? std::string() : " " + yUnit_;
    if (!ImGui::BeginTooltip()) return;
    ImGui::Text("Point %d", i + 1);
    ImGui::Separator();
    ImGui::Text("%s = %s%s", std::string(def.xName).c_str(), kin::fmt_g(a.data[k].x, 10).c_str(), xu.c_str());
    ImGui::Text("%s = %s%s", std::string(def.yName).c_str(), kin::fmt_g(a.data[k].y, 10).c_str(), yu.c_str());
    if (k < a.yhat.size() && k < a.resid.size()) {
        ImGui::TextDisabled("Fitted %s = %s, residual %s%s", std::string(def.yName).c_str(),
                            kin::fmt_g(a.yhat[k], 6).c_str(), a.resid[k] >= 0.0 ? "+" : "",
                            kin::fmt_g(a.resid[k], 4).c_str());
        if (k < a.largeResidual.size() && a.largeResidual[k])
            ImGui::TextColored(palette().warning, "2 residual SDs or more from the curve");
    }
    ImGui::EndTooltip();
}

float App::plot_min_height(float width) const {
    const float fs = ImGui::GetFontSize();
    const float graph = std::max(width / 3.0f, fs * 10.0f);
    const bool strip = residualStrip_ && plotType_ == kin::PlotType::Normal && analysis_ && !analysis_->data.empty();
    return strip ? graph + std::max(width / 4.0f, fs * 6.0f) : graph;
}

void App::draw_plot_controls() {
    if (!analysis_) return;
    const auto m = analysis_->model();
    const auto plots = kin::available_plots(m);
    const bool residual = plotType_ == kin::PlotType::Residual;
    // The X intercept says something only on the straight-line plots.
    const bool interceptMeaningful = !residual && (plotType_ != kin::PlotType::Normal || m == kin::ModelId::Linear);

    field_label("Plot");
    ImGui::SetNextItemWidth(std::min(ImGui::GetFontSize() * 18.0f, ImGui::GetContentRegionAvail().x * 0.45f));
    if (begin_combo("##plottype", kin::plot_name(plotType_, m).c_str(), ImGuiComboFlags_HeightLarge)) {
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
    if (begin_combo("##display", "Display", ImGuiComboFlags_HeightLarge)) {
        changed |= ImGui::Checkbox("Include the origin", &plotOptions_.includeOrigin);
        if (!residual) changed |= ImGui::Checkbox("Extend the curve to the Y axis", &plotOptions_.extendToYAxis);
        if (interceptMeaningful) {
            changed |= ImGui::Checkbox("Extend the line to the X intercept", &plotOptions_.extendToXIntercept);
            ImGui::SetItemTooltip("Draws the line to where it crosses Y = 0 and labels that X value");
        }
        bool save = ImGui::Checkbox("Number the points", &numberPoints_);
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
    same_line_if_fits(button_width("Copy results") + helpW);
    if (ImGui::Button("Copy results")) copy_results();
    ImGui::SetItemTooltip("Copy the parameters, and each point with its fitted value and residual, with all digits, "
                          "as tables to paste into a spreadsheet. The Report tab has the full report.");
    help_link(HelpId::Plotting);
}

void App::draw_plot(const ImVec2& size) {
    if (!analysis_) return;
    const auto& a = *analysis_;
    const auto m = a.model();
    const auto& def = kin::model_def(m);
    const bool residual = plotType_ == kin::PlotType::Residual;
    const bool interceptMeaningful = !residual && (plotType_ != kin::PlotType::Normal || m == kin::ModelId::Linear);
    // The residual strip is a quarter of the plot's width high, and the graph
    // above it gets the rest (the caller leaves it at least a third of the
    // width; see plot_min_height).
    const float fs = ImGui::GetFontSize();
    const float stripH = std::max(size.x / 4.0f, fs * 6.0f);
    const bool strip = residualStrip_ && plotType_ == kin::PlotType::Normal && !a.data.empty();
    const auto pd = kin::make_plot(a, plotType_, plotOptions_);
    const auto predictions = prediction_points(a);

    // Units for the axes of the normal plot, whose axes are the data's.
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
        // No legend: the parameters are just above the graph.
        if (!ImPlot::BeginPlot("##graph", plotSize, ImPlotFlags_NoTitle | ImPlotFlags_NoLegend)) return;
        // With the residual strip below, the X axis's title goes under the
        // strip, and its numbers under both.
        ImPlot::SetupAxes(strip ? nullptr : xLabel.c_str(), yLabel.c_str());
        if (refit) ImPlot::SetupAxesLimits(pd.xmin, pd.xmax, pd.ymin, pd.ymax, ImPlotCond_Always);
        origin_axes(true);

        if (!pd.cx.empty()) {
            ImPlotSpec curve;
            curve.LineColor = faded(palette().accent);
            curve.LineWeight = 2.0f;
            ImPlot::PlotLine("Fitted curve", pd.cx.data(), pd.cy.data(), static_cast<int>(pd.cx.size()), curve);
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

        // The Calculate box's answers, each a cross of its colour on the curve.
        for (int k = 0; k < 2; ++k) {
            const auto& pt = predictions[static_cast<size_t>(k)];
            if (!pt) continue;
            const auto [X, Y] = kin::plot_coordinates(a, plotType_, pt->first, pt->second);
            if (!std::isfinite(X) || !std::isfinite(Y)) continue;
            ImPlotSpec cross;
            cross.Marker = ImPlotMarker_Cross;
            cross.MarkerSize = 7.0f;
            cross.LineWeight = 2.5f;
            cross.LineColor = faded(prediction_colour(k));
            cross.Flags = ImPlotItemFlags_NoFit;
            const double xs[1] = {X}, ys[1] = {Y};
            ImPlot::PlotScatter(k == 0 ? "##predict0" : "##predict1", xs, ys, 1, cross);
        }

        // The point under the mouse: its exact values, and its row in the
        // data table highlighted.
        if (const int k = point_under_mouse(pd.px, pd.py); k >= 0) {
            ring(pd.px[static_cast<size_t>(k)], pd.py[static_cast<size_t>(k)]);
            hoverNext_ = pd.labels[static_cast<size_t>(k)] - 1;
            point_tooltip(a, hoverNext_);
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
        // Residuals of 2 SDs or more, in amber.
        pts.LineColor = faded(palette().warning);
        pts.MarkerFillColor = faded(palette().warning);
        ImPlot::PlotScatter("Large residual", bx.data(), by.data(), static_cast<int>(bx.size()), pts);
        // As on the graph: the values of the point under the mouse.
        std::vector<double> ax, ay;
        for (size_t i = 0; i < a.data.size() && i < a.resid.size(); ++i) {
            ax.push_back(a.data[i].x);
            ay.push_back(a.resid[i]);
        }
        if (const int k = point_under_mouse(ax, ay); k >= 0) {
            ring(ax[static_cast<size_t>(k)], ay[static_cast<size_t>(k)]);
            hoverNext_ = k;
            point_tooltip(a, k);
        }
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
