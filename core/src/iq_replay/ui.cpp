#include "ui.h"
#include "rows.h"
#include <audio/rewind.h>
#include <gui/style.h>
#include <algorithm>
#include <map>
#include <limits>
#include <http_debug_server.h>

namespace beige::iqreplay {
namespace {
struct Map {
    Rows rows;
    std::vector<Stamp> displayed;
    ImVec2 min{}, max{};
    double low = 0, high = 0;
    bool seekingGesture = false;
    bool tuningGesture = false;
    ImVec2 lastClick{};
    int selectedRow = -1;
};
// Waterfalls also exist in static GUI constructors, before this translation
// unit's initializers. Construct sidecars on first use to avoid init-order UB.
std::map<void*, Map>& maps() { static std::map<void*, Map> value; return value; }
bool inside(ImVec2 p, ImVec2 min, ImVec2 max) {
    return std::isfinite(p.x) && std::isfinite(p.y) && p.x >= min.x && p.x < max.x && p.y >= min.y && p.y < max.y;
}
}
void resizeRows(void* owner, int oldHead, int oldHeight, int height) {
    auto& map = maps()[owner]; map.rows.resize(oldHead, oldHeight, height); map.displayed.clear();
    layout(height);
}
void clearRows(void* owner) { maps()[owner].rows.clear(); maps()[owner].displayed.clear(); }
void pushRow(void* owner, int index, int height) {
    if (enabled()) maps()[owner].rows.put(index, height, frameStamp());
}
bool handleInput(void* owner, ImVec2 min, ImVec2 max, ImVec2 spectrumMin, ImVec2 spectrumMax,
    double low, double high, double snap, const std::string& receiver) {
    auto& map = maps()[owner];
    const auto p = ImGui::GetMousePos();
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) map.seekingGesture = map.tuningGesture = false;
    const bool hovered = inside(p, min, max) && ImGui::IsWindowHovered();
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        ImGui::OpenPopup("beige_waterfall_context"); return true;
    }
    const auto s = state();
    const bool replay = s.replaying && s.receiver == receiver;
    if (!replay) map.tuningGesture = false;
    const bool spectrumHovered = replay && inside(p, spectrumMin, spectrumMax) && ImGui::IsWindowHovered();
    if (!enabled() || (!hovered && !spectrumHovered && !map.tuningGesture)) return false;
    const auto tuneAtPointer = [&] {
        if (spectrumMax.x <= spectrumMin.x || p.x < spectrumMin.x || p.x >= spectrumMax.x || !std::isfinite(p.x)) return;
        double frequency = low + (p.x - spectrumMin.x) / (spectrumMax.x - spectrumMin.x) * (high - low);
        if (snap > 0) frequency = std::round(frequency / snap) * snap;
        if (frequency != s.frequency) tune(frequency);
    };
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        // The splitter's existing hit target overlaps the first two rows.
        if (hovered && p.y <= min.y + 2 * style::uiScale) return false;
        map.seekingGesture = true;
        // Before replay, reserve the first click so center tuning cannot erase
        // the historical point. During replay, clicks and drags tune only IQ.
        if (!hovered || !ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            if (replay) { map.tuningGesture = true; tuneAtPointer(); }
            return true;
        }
        // A resize invalidates the previous layout; never reinterpret its pixels.
        if (min.x != map.min.x || min.y != map.min.y || max.x != map.max.x || max.y != map.max.y ||
            map.max.x <= map.min.x || map.max.y <= map.min.y) {
            notice("The waterfall layout changed. Select the point again."); return true;
        }
        const auto at = Rows::point(map.displayed, p.y - map.min.y);
        map.lastClick = p; map.selectedRow = int(std::floor(p.y - map.min.y));
        const double frequency = map.low + (p.x - map.min.x) / (map.max.x - map.min.x) * (map.high - map.low);
        seek(receiver, at, frequency);
        return true;
    }
    if (replay && map.tuningGesture && ImGui::IsMouseDown(ImGuiMouseButton_Left)) { tuneAtPointer(); return true; }
    if (map.seekingGesture && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) map.seekingGesture = false;
    return map.seekingGesture && ImGui::IsMouseDown(ImGuiMouseButton_Left);
}
void drawMarker(ImDrawList* draw, ImVec2 min, ImVec2 max, double low, double high) {
    const auto s = state();
    if (!s.replaying || high <= low || s.frequency < low || s.frequency >= high) return;
    const float x = min.x + float((s.frequency - low) / (high - low)) * (max.x - min.x);
    draw->AddLine(ImVec2(x, min.y), ImVec2(x, max.y), IM_COL32(255, 192, 76, 220));
    draw->AddText(ImVec2(x + 4, min.y + 4), IM_COL32(255, 205, 115, 255), "IQ");
}
void drawMap(void* owner, ImDrawList* draw, ImVec2 min, ImVec2 max, int head, int filled,
    int height, double low, double high) {
    static bool registered = false;
    if (!registered) {
        registered = true;
        httpdebug::procfs::registerEndpoint("/iq-replay/map", [owner] {
            auto it = maps().find(owner);
            if (it == maps().end()) return std::string("{}");
            const auto& m = it->second;
            nlohmann::json rows = nlohmann::json::array();
            for (const auto& at : m.displayed) rows.push_back({at.epoch, at.sample});
            return nlohmann::json{{"min", {m.min.x, m.min.y}}, {"max", {m.max.x, m.max.y}},
                {"low", m.low}, {"high", m.high}, {"rows", rows},
                {"last_click", {m.lastClick.x, m.lastClick.y}}, {"selected_row", m.selectedRow}}.dump();
        }, nullptr, httpdebug::procfs::Type::String);
    }
    if (ImGui::BeginPopup("beige_waterfall_context")) {
        if (ImGui::MenuItem("Live")) rewind::live();
        ImGui::EndPopup();
    }
    if (!enabled()) return;
    auto& map = maps()[owner];
    map.displayed = map.rows.snapshot(head, filled, height);
    map.min = min; map.max = max; map.low = low; map.high = high;
    const auto s = state();
    const double retained = s.storage.rate ? double(s.storage.head - s.storage.oldest()) / s.storage.rate : 0;
    char label[192];
    if (s.replaying) {
        const double delay = s.storage.rate && s.storage.captured >= s.cursor.sample
            ? double(s.storage.captured - s.cursor.sample) / s.storage.rate : 0;
        snprintf(label, sizeof(label), "IQ REPLAY | %.6f MHz | -%.2fs", s.frequency / 1e6, delay);
        if (high > low && s.frequency >= low && s.frequency < high) {
            const float x = min.x + float((s.frequency - low) / (high - low)) * (max.x - min.x);
            draw->AddLine(ImVec2(x, min.y), ImVec2(x, max.y), IM_COL32(255, 192, 76, 220));
        }
        uint64_t best = std::numeric_limits<uint64_t>::max(); int row = -1;
        for (size_t i = 0; i < map.displayed.size(); ++i) {
            const auto& stamp = map.displayed[i];
            if (stamp.epoch != s.cursor.epoch) continue;
            const auto distance = stamp.sample > s.cursor.sample ? stamp.sample - s.cursor.sample : s.cursor.sample - stamp.sample;
            if (distance < best) { best = distance; row = int(i); }
        }
        if (row >= 0) draw->AddLine(ImVec2(min.x, min.y + row), ImVec2(max.x, min.y + row), IM_COL32(255, 192, 76, 180));
    } else snprintf(label, sizeof(label), "IQ %s | %.1fs retained", s.storage.starting ? "PREPARING" : "LIVE", retained);
    const auto textSize = ImGui::CalcTextSize(label);
    draw->AddRectFilled(ImVec2(min.x + 3, min.y + 3), ImVec2(min.x + textSize.x + 13, min.y + textSize.y + 9), IM_COL32(0, 0, 0, 190), 3);
    draw->AddText(ImVec2(min.x + 8, min.y + 6), s.replaying ? IM_COL32(255, 205, 115, 255) : IM_COL32(173, 220, 203, 255), label);
    const auto p = ImGui::GetMousePos();
    if (inside(p, min, max) && ImGui::IsWindowHovered() && !ImGui::IsPopupOpen("beige_waterfall_context")) {
        const auto at = Rows::point(map.displayed, p.y - min.y);
        if (at && at.epoch == s.storage.epoch && at.sample >= s.storage.oldest() && at.sample < s.storage.captured) {
            const double frequency = low + (p.x - min.x) / (max.x - min.x) * (high - low);
            ImGui::SetTooltip(s.replaying
                ? "Replay %.6f MHz at -%.3fs\nClick or drag to tune at the current replay time.\nDouble-click to change time and frequency.\nUp/Down: newer/older by 1s (Shift: 10s, Alt: 0.1s).\nPlugins remain live."
                : "Replay %.6f MHz at -%.3fs\nDouble-click to listen. Plugins remain live.", frequency / 1e6,
                double(s.storage.captured - at.sample) / at.rate);
        }
    }
}
void drawSetting() {
    bool on = enabled();
    if (ImGui::Checkbox("Waterfall replay", &on)) enable(on);
    if (ImGui::IsItemHovered()) {
        const auto s = state();
        const double bytes = s.storage.capacity * sizeof(dsp::complex_t);
        ImGui::SetTooltip("Keep full-band IQ for the visible waterfall in temporary disk storage.\nDouble-click a waterfall point to replay its time and frequency through Radio.\nPlugins and recordings stay live. Live returns both transports to current RF.\nStorage follows waterfall height and speed, with a safety margin.\nCurrent buffer: %.1f seconds, %.1f MB. Deleted when disabled or Beige exits.",
            s.desiredSeconds, bytes / 1e6);
    }
    const auto s = state();
    if (!s.error.empty()) { ImGui::TextDisabled("IQ replay off: storage/processing error"); if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", s.error.c_str()); }
}
void clearMaps() { maps().clear(); }
}
