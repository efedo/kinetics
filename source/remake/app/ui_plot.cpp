// Graph tab (v3.51 View > Graph and PLOT.CPP), drawn with ImPlot.

#include <imgui.h>
#include <implot.h>

#include <cfloat>
#include <cstdio>

#include "app/app.hpp"
#include "app/theme.hpp"
#include "app/ui_common.hpp"

namespace app {

void App::draw_graph_tab() {
    if (!analysis_) {
        no_results_text();
        return;
    }
    const auto& a = *analysis_;
    const auto m = a.model();
    const auto plots = kin::available_plots(m);

    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 22.0f);
    if (ImGui::BeginCombo("Plot", kin::plot_name(plotType_, m).c_str(), ImGuiComboFlags_HeightLarge)) {
        for (auto t : plots)
            if (ImGui::Selectable(kin::plot_name(t, m).c_str(), t == plotType_) && t != plotType_) set_plot_type(t);
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset view")) refitPlot_ = true;

    const bool residual = plotType_ == kin::PlotType::Residual;
    bool changed = ImGui::Checkbox("Include origin", &plotOptions_.includeOrigin);
    if (!residual) {
        ImGui::SameLine();
        changed |= ImGui::Checkbox("Extend curve to Y axis", &plotOptions_.extendToYAxis);
        ImGui::SameLine();
        changed |= ImGui::Checkbox("Extend curve to X intercept", &plotOptions_.extendToXIntercept);
    }
    ImGui::SameLine();
    ImGui::Checkbox("Number the points", &numberPoints_);
    if (changed) refitPlot_ = true;

    const auto pd = kin::make_plot(a, plotType_, plotOptions_);

    if (ImPlot::BeginPlot("##graph", ImGui::GetContentRegionAvail(), ImPlotFlags_NoTitle)) {
        ImPlot::SetupAxes(pd.xLabel.c_str(), pd.yLabel.c_str());
        if (refitPlot_) {
            ImPlot::SetupAxesLimits(pd.xmin, pd.xmax, pd.ymin, pd.ymax, ImPlotCond_Always);
            refitPlot_ = false;
        }
        ImPlot::SetupLegend(ImPlotLocation_North, ImPlotLegendFlags_Outside | ImPlotLegendFlags_Horizontal);

        // Axes through the origin, as in v3.51's plots.
        {
            ImPlotSpec axis;
            const ImVec4 dim = palette().textDim;
            axis.LineColor = ImVec4(dim.x, dim.y, dim.z, 0.6f);
            axis.Flags = ImPlotItemFlags_NoLegend | ImPlotItemFlags_NoFit;
            const double zero = 0.0;
            ImPlot::PlotInfLines("##x0", &zero, 1, axis);
            axis.Flags |= ImPlotInfLinesFlags_Horizontal;
            ImPlot::PlotInfLines("##y0", &zero, 1, axis);
        }

        if (!pd.cx.empty()) {
            ImPlotSpec curve;
            curve.LineColor = palette().accent;
            curve.LineWeight = 2.0f;
            ImPlot::PlotLine("Fitted curve", pd.cx.data(), pd.cy.data(), static_cast<int>(pd.cx.size()), curve);
        }

        ImPlotSpec pts;
        pts.Marker = ImPlotMarker_Circle;
        pts.MarkerSize = 4.5f;
        pts.LineColor = palette().text;
        pts.MarkerFillColor = palette().text;
        ImPlot::PlotScatter("Data", pd.px.data(), pd.py.data(), static_cast<int>(pd.px.size()), pts);

        if (numberPoints_) {
            ImPlotSpec label;
            label.LineColor = palette().textDim;
            for (size_t i = 0; i < pd.px.size(); ++i) {
                if (!std::isfinite(pd.px[i]) || !std::isfinite(pd.py[i])) continue;
                char buf[16];
                std::snprintf(buf, sizeof buf, "%d", pd.labels[i]);
                ImPlot::PlotText(buf, pd.px[i], pd.py[i], ImVec2(9.0f, -9.0f), label);
            }
        }

        const bool meaningful = plotType_ != kin::PlotType::Normal || m == kin::ModelId::Linear;
        if (!residual && meaningful && plotOptions_.extendToXIntercept && pd.xIntercept) {
            ImPlot::Annotation(*pd.xIntercept, 0.0, palette().accent, ImVec2(0, 18), true,
                               "X = %s", kin::fmt_g(*pd.xIntercept, 4).c_str());
        }
        ImPlot::EndPlot();
    }
}

}  // namespace app
