#define IMGUI_DEFINE_MATH_OPERATORS
#include "ham_band_toolbar.h"
#include "band_presets.h"
#include <gui/gui.h>
#include <gui/style.h>
#include <gui/tuner.h>
#include <gui/widgets/snr_meter.h>
#include <core.h>
#include <http_debug_server.h>
#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <string>

namespace beige::bandtoolbar {
namespace {

BandCategory category = BandCategory::Ham;
struct ButtonRect { float x, y, width, height; bool hovered; };
struct ToolbarLayout {
    BandCategory group = BandCategory::Ham;
    float selectorX = 0, selectorY = 0, selectorWidth = 0, bottom = 0;
    float meterX = 0, meterWidth = 0, windowWidth = 0;
    bool below = false;
    std::array<ButtonRect, maxBandCount> buttons{};
} layout;

void setCategory(BandCategory next) {
    if (category == next) { return; }
    category = next;
    core::configManager.acquire();
    core::configManager.conf["bandShortcutCategory"] = categoryLabel(category);
    core::configManager.release(true);
}

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

void drawTooltip(const HamBand& band, BandCategory group) {
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
    ImGui::TextDisabled(group == BandCategory::Ham ? "BEST TIME TO OPERATE" : "BEST TIME TO LISTEN");
    ImGui::PopFont();
    ImGui::TextUnformatted(band.bestTime);
    ImGui::Spacing();
    ImGui::PushFont(style::tinyFont);
    ImGui::TextDisabled("TYPICAL USE");
    ImGui::PopFont();
    ImGui::TextUnformatted(band.typicalUse);
    ImGui::Spacing();
    ImGui::PushFont(style::tinyFont);
    ImGui::TextDisabled(group == BandCategory::Ham ? "TYPICAL DISTANCE" : "TYPICAL COVERAGE");
    ImGui::PopFont();
    ImGui::TextUnformatted(band.typicalDistance);
    ImGui::Spacing();
    ImGui::PushFont(style::tinyFont);
    ImGui::TextDisabled(group == BandCategory::Ham
        ? "Approximate routine ranges; setup and conditions matter."
        : "Receive search range. Activity varies by location and propagation. Choose the mode in Radio.");
    ImGui::PopFont();
    ImGui::PopTextWrapPos();
    ImGui::PopFont();
    ImGui::EndTooltip();
    ImGui::PopStyleVar(3);
}

bool useDarkText(const HamBand& band, BandCategory group, float factor) {
    if (group == BandCategory::Ham) { return band.color == 0xCE891E || band.color == 0xDEB52D; }
    const auto color = tint(band.color, factor);
    const auto linear = [](float c) { return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); };
    const float luminance = 0.2126f * linear(color.x) + 0.7152f * linear(color.y) + 0.0722f * linear(color.z);
    return luminance > 0.179f;
}

} // namespace

void init() {
    core::configManager.acquire();
    const auto saved = core::configManager.conf.find("bandShortcutCategory");
    category = BandCategory::Ham;
    if (saved != core::configManager.conf.end() && saved->is_string()) {
        parseCategory(saved->get<std::string>(), category);
    }
    core::configManager.release();
    httpdebug::procfs::registerEndpoint("/display/band-category", []() {
        return std::string(categoryLabel(category));
    }, [](const std::string& label) {
        BandCategory next;
        if (parseCategory(label, next)) { setCategory(next); }
    }, httpdebug::procfs::Type::String);
    httpdebug::procfs::registerEndpoint("/display/band", []() {
        for (const auto& band : bandsFor(category)) {
            if (band.contains(gui::freqSelect.frequency)) { return std::string(band.label); }
        }
        return std::string{};
    }, [](const std::string& label) {
        for (const auto& band : bandsFor(category)) {
            if (label == band.label) { select(band); return; }
        }
    }, httpdebug::procfs::Type::String);
    httpdebug::procfs::registerEndpoint("/display/band-catalog", []() {
        json groups = json::object();
        for (const auto group : bandCategories) {
            auto entries = json::array();
            for (const auto& band : bandsFor(group)) {
                entries.push_back({{"label", band.label}, {"lower", band.lowerHz},
                    {"upper", band.upperHz}, {"color", band.color}});
            }
            groups[categoryLabel(group)] = entries;
        }
        return groups.dump();
    }, nullptr, httpdebug::procfs::Type::String);
    httpdebug::procfs::registerEndpoint("/display/band-layout", []() {
        auto rectangles = json::array();
        const auto bands = bandsFor(layout.group);
        for (std::size_t i = 0; i < bands.size; ++i) {
            const auto& r = layout.buttons[i];
            rectangles.push_back({{"label", bands[i].label}, {"x", r.x}, {"y", r.y},
                {"width", r.width}, {"height", r.height}, {"hovered", r.hovered}});
        }
        return json{{"category", categoryLabel(layout.group)}, {"selector_x", layout.selectorX},
            {"selector_y", layout.selectorY}, {"selector_width", layout.selectorWidth},
            {"bottom", layout.bottom}, {"below", layout.below}, {"meter_x", layout.meterX},
            {"meter_width", layout.meterWidth}, {"window_width", layout.windowWidth},
            {"buttons", rectangles}}.dump();
    }, nullptr, httpdebug::procfs::Type::String);
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
    const auto group = category;
    const auto bands = bandsFor(group);
    // Reserve the full 28-point combo arrow plus both frame paddings.
    const float selectorWidth = ImGui::CalcTextSize("Broadcast").x + 44 * scale;
    std::array<float, maxBandCount> widths{};
    float singleWidth = 0;
    for (std::size_t i = 0; i < bands.size; ++i) {
        widths[i] = ImGui::CalcTextSize(bands[i].label).x + 8 * scale;
        singleWidth += widths[i] + (i ? gap : 0);
    }
    const float meterWidth = std::clamp(available - selectorWidth - gap - singleWidth - 16 * scale, 120 * scale, 300 * scale);
    const float bandWidth = available - meterWidth - 16 * scale - selectorWidth - gap;
    const bool below = bandWidth < twoRowWidth(widths.data(), bands.size, gap);
    const ImVec2 bandOrigin = below ? ImVec2(8 * scale, topY + 46 * scale) : origin;
    const ImVec2 buttonsOrigin = bandOrigin + ImVec2(selectorWidth + gap, 0);
    const float width = below ? ImGui::GetWindowSize().x - 16 * scale - selectorWidth - gap : bandWidth;
    const bool oneRow = singleWidth <= width;
    const float height = (oneRow ? 28 : 19) * scale;
    float x = 0, y = oneRow && !below ? 6 * scale : 0;
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3 * scale);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 2) * scale);
    ImGui::PushID("beige_band_shortcuts");
    ImGui::SetCursorPos(bandOrigin + ImVec2(0, y));
    ImGui::SetNextItemWidth(selectorWidth);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6 * scale, (height - ImGui::GetFontSize()) * 0.5f));
    if (ImGui::BeginCombo("###category", categoryLabel(group), ImGuiComboFlags_HeightSmall)) {
        for (const auto choice : bandCategories) {
            const bool active = choice == category;
            if (ImGui::Selectable(categoryLabel(choice), active)) { setCategory(choice); }
            if (active) { ImGui::SetItemDefaultFocus(); }
        }
        ImGui::EndCombo();
    }
    ImGui::PopStyleVar();
    for (std::size_t i = 0; i < bands.size; ++i) {
        const auto& band = bands[i];
        if (x && x + widths[i] > width) { x = 0; y += height + 2 * scale; }
        ImGui::SetCursorPos(buttonsOrigin + ImVec2(x, y));
        const bool selected = band.contains(gui::freqSelect.frequency);
        const bool darkText = useDarkText(band, group, selected ? 1.0f : 0.86f);
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
        if (hovered) { drawTooltip(band, group); }
        layout.buttons[i] = {buttonsOrigin.x + x, buttonsOrigin.y + y, widths[i], height, hovered};
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
    layout.group = group;
    layout.selectorX = bandOrigin.x;
    layout.selectorY = bandOrigin.y + (oneRow && !below ? 6 * scale : 0);
    layout.selectorWidth = selectorWidth;
    layout.bottom = bottom;
    layout.below = below;
    layout.meterX = right - meterWidth;
    layout.meterWidth = meterWidth;
    layout.windowWidth = ImGui::GetWindowSize().x;
    ImGui::SetCursorPos(ImVec2(8 * scale, topY));
    ImGui::Dummy(ImVec2(right - 8 * scale, bottom - topY));
}

} // namespace beige::bandtoolbar
