#define IMGUI_DEFINE_MATH_OPERATORS
#include "ham_band_toolbar.h"
#include "ham_bands.h"
#include <gui/gui.h>
#include <gui/style.h>
#include <gui/tuner.h>
#include <gui/widgets/snr_meter.h>
#include <core.h>
#include <http_debug_server.h>
#include <algorithm>
#include <array>
#include <cfloat>
#include <string>

namespace beige::bandtoolbar {
namespace {

void select(const HamBand& band) {
    const auto name = gui::waterfall.selectedVFO;
    // Center this jump without changing MainWindow's persistent tuning mode.
    tuner::centerTuning(name, (double)band.centerHz());
    gui::freqSelect.frequencyChanged = false;
    gui::waterfall.centerFreqMoved = false;
    const auto vfo = gui::waterfall.vfos.find(name);
    if (vfo != gui::waterfall.vfos.end()) {
        vfo->second->centerOffsetChanged = false;
        vfo->second->lowerOffsetChanged = false;
        vfo->second->upperOffsetChanged = false;
    }
    core::configManager.acquire();
    core::configManager.conf["frequency"] = gui::waterfall.getCenterFrequency();
    if (vfo != gui::waterfall.vfos.end()) {
        core::configManager.conf["vfoOffsets"][name] = vfo->second->generalOffset;
    }
    core::configManager.release(true);
}

ImVec4 tint(uint32_t rgb, float factor) {
    return {((rgb >> 16) & 255) / 255.0f * factor,
            ((rgb >> 8) & 255) / 255.0f * factor,
            (rgb & 255) / 255.0f * factor, 1};
}

void drawTooltip(const HamBand& band) {
    const float scale = style::uiScale;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14, 12) * scale);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 5 * scale);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 6) * scale);
    ImGui::SetNextWindowSizeConstraints(ImVec2(320 * scale, 0), ImVec2(320 * scale, FLT_MAX));
    ImGui::SetNextWindowBgAlpha(1.0f);
    ImGui::BeginTooltip();
    ImGui::PushFont(style::baseFont);
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 292 * scale);
    ImGui::PushFont(style::mediumFont);
    ImGui::TextColored(tint(band.color, 1.3f), "%s band", band.label);
    ImGui::PopFont();
    ImGui::TextDisabled("%.3f - %.3f MHz", band.lowerHz / 1e6, band.upperHz / 1e6);
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::PushFont(style::tinyFont);
    ImGui::TextDisabled("BEST TIME TO OPERATE");
    ImGui::PopFont();
    ImGui::TextUnformatted(band.bestTime);
    ImGui::Spacing();
    ImGui::PushFont(style::tinyFont);
    ImGui::TextDisabled("TYPICAL USE");
    ImGui::PopFont();
    ImGui::TextUnformatted(band.typicalUse);
    ImGui::Spacing();
    ImGui::PushFont(style::tinyFont);
    ImGui::TextDisabled("TYPICAL DISTANCE");
    ImGui::PopFont();
    ImGui::TextUnformatted(band.typicalDistance);
    ImGui::Spacing();
    ImGui::PushFont(style::tinyFont);
    ImGui::TextDisabled("Approximate routine ranges; setup and conditions matter.");
    ImGui::PopFont();
    ImGui::PopTextWrapPos();
    ImGui::PopFont();
    ImGui::EndTooltip();
    ImGui::PopStyleVar(3);
}

} // namespace

void init() {
    httpdebug::procfs::registerEndpoint("/display/ham-band", []() {
        for (const auto& band : hamBands) {
            if (band.contains(gui::freqSelect.frequency)) { return std::string(band.label); }
        }
        return std::string{};
    }, [](const std::string& label) {
        for (const auto& band : hamBands) {
            if (label == band.label) { select(band); return; }
        }
    }, httpdebug::procfs::Type::String);
    httpdebug::procfs::registerEndpoint("/display/ham-band-state", []() {
        const auto name = gui::waterfall.selectedVFO;
        const auto vfo = gui::waterfall.vfos.find(name);
        core::configManager.acquire();
        const bool center = core::configManager.conf.value("centerTuning", false);
        core::configManager.release();
        return json{{"frequency", gui::freqSelect.frequency},
            {"spectrum_center", gui::waterfall.getCenterFrequency()},
            {"view_offset", gui::waterfall.getViewOffset()},
            {"vfo_offset", vfo == gui::waterfall.vfos.end() ? 0 : vfo->second->generalOffset},
            {"center_tuning", center}, {"single_click_vfo", gui::waterfall.VFOMoveSingleClick}}.dump();
    }, nullptr, httpdebug::procfs::Type::String);
}

void draw(float snr, float topY) {
    ImGui::PushFont(style::tinyFont);
    const float scale = style::uiScale;
    const float gap = 3 * scale;
    const ImVec2 origin(ImGui::GetCursorPosX(), topY);
    const float right = ImGui::GetWindowSize().x - 80 * scale;
    const float available = right - origin.x;
    std::array<float, hamBands.size()> widths{};
    float rowWidths[2] = {0, 0}, singleWidth = 0;
    for (std::size_t i = 0; i < hamBands.size(); ++i) {
        widths[i] = ImGui::CalcTextSize(hamBands[i].label).x + 8 * scale;
        rowWidths[i / 8] += widths[i] + (i % 8 ? gap : 0);
        singleWidth += widths[i] + (i ? gap : 0);
    }
    const float meterWidth = std::clamp(available - singleWidth - 16 * scale, 120 * scale, 300 * scale);
    const float bandWidth = available - meterWidth - 16 * scale;
    const bool below = bandWidth < (std::max)(rowWidths[0], rowWidths[1]);
    const ImVec2 bandOrigin = below ? ImVec2(8 * scale, topY + 46 * scale) : origin;
    const float width = below ? ImGui::GetWindowSize().x - 16 * scale : bandWidth;
    const bool oneRow = singleWidth <= width;
    const float height = (oneRow ? 28 : 19) * scale;
    float x = 0, y = oneRow && !below ? 6 * scale : 0;
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3 * scale);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 2) * scale);
    ImGui::PushID("beige_ham_bands");
    for (std::size_t i = 0; i < hamBands.size(); ++i) {
        const auto& band = hamBands[i];
        if (x && x + widths[i] > width) { x = 0; y += height + 2 * scale; }
        ImGui::SetCursorPos(bandOrigin + ImVec2(x, y));
        const bool selected = band.contains(gui::freqSelect.frequency);
        const bool darkText = band.color == 0xCE891E || band.color == 0xDEB52D;
        ImGui::PushStyleColor(ImGuiCol_Button, tint(band.color, selected ? 1.0f : 0.86f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, tint(band.color, 1.10f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, tint(band.color, 0.72f));
        ImGui::PushStyleColor(ImGuiCol_Text, darkText ? ImVec4(0.08f, 0.06f, 0.02f, 1) : ImVec4(0.98f, 0.98f, 0.96f, 1));
        ImGui::PushStyleColor(ImGuiCol_Border, selected ? ImVec4(1, 0.97f, 0.83f, 0.95f) : tint(band.color, 1.15f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, selected ? 1.5f * scale : 0.5f * scale);
        if (ImGui::Button(band.label, ImVec2(widths[i], height))) { select(band); }
        const bool hovered = ImGui::IsItemHovered();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(5);
        if (hovered) { drawTooltip(band); }
        x += widths[i] + gap;
    }
    ImGui::PopID();
    ImGui::PopStyleVar(2);
    ImGui::PopFont();
    ImGui::SetCursorPos(ImVec2(right - meterWidth, topY + 5 * scale));
    ImGui::SetNextItemWidth(meterWidth);
    if (meterWidth < 180 * scale) { ImGui::PushFont(style::tinyFont); }
    ImGui::SNRMeter(snr);
    if (meterWidth < 180 * scale) { ImGui::PopFont(); }
    // Reserve the entire toolbar height even after the caller places its logo.
    const float bottom = (std::max)(topY + 40 * scale, bandOrigin.y + y + height);
    ImGui::SetCursorPos(ImVec2(8 * scale, topY));
    ImGui::Dummy(ImVec2(right - 8 * scale, bottom - topY));
}

} // namespace beige::bandtoolbar
