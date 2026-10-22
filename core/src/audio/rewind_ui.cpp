#include "rewind.h"
#include <imgui.h>
#include <gui/style.h>
#include <algorithm>

namespace beige::rewind {
namespace {
void tip(const char* text) { if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", text); }
}

float barHeight() {
    const bool split = ImGui::GetContentRegionAvail().x < 700 * style::uiScale;
    return ImGui::GetFrameHeight() + ImGui::GetTextLineHeightWithSpacing() + 12 * style::uiScale
        + (split ? ImGui::GetFrameHeight() + 3 * style::uiScale : 0);
}

void draw(const std::string& streamName, bool receiving) {
    select(streamName);
    setReceiving(receiving);
    const auto s = state();
    const auto e = exportState();
    const float width = ImGui::GetContentRegionAvail().x;
    const bool split = width < 700 * style::uiScale;
    const bool roomy = width > 850 * style::uiScale;
    ImGui::PushID("beige_audio_rewind");
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6 * style::uiScale, 3 * style::uiScale));
    bool on = enabled();
    if (ImGui::Checkbox("Rewind", &on)) enable(on);
    tip("Keep the selected receiver's last 60 seconds of audio in memory.\nTurning this off clears the history and returns to live audio.");
    ImGui::SameLine();
    const bool hasAudio = on && s.available > .05;
    if (!hasAudio) ImGui::BeginDisabled();
    const struct { const char* label; int seconds; } steps[] = {{"-3s", 3}, {"-5s", 5}, {"-10s", 10}, {"-30s", 30}};
    for (const auto& step : steps) {
        if (ImGui::Button(step.label, ImVec2(46 * style::uiScale, 0))) { seek(s.delay + step.seconds); pause(false); }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Rewind %d seconds from the current position.\nReception continues; limited to available history.", step.seconds);
        ImGui::SameLine();
    }
    if (ImGui::Button(s.paused ? "Play###pause" : "Pause###pause", ImVec2(58 * style::uiScale, 0))) pause(!s.paused);
    tip("Pause or resume audio. Incoming audio continues filling the history.\nThe oldest audio expires after 60 seconds, including while paused.");
    ImGui::SameLine();
    const ImVec4 liveColor = s.live ? ImVec4(.12f, .42f, .30f, 1) : ImVec4(.48f, .31f, .10f, 1);
    ImGui::PushStyleColor(ImGuiCol_Button, liveColor);
    if (ImGui::Button("Live", ImVec2(42 * style::uiScale, 0))) live();
    ImGui::PopStyleColor();
    tip("Return immediately to live audio.");
    if (!split) ImGui::SameLine();
    float position = -float(s.delay);
    const float reserve = (roomy ? 228 : 142) * style::uiScale;
    ImGui::SetNextItemWidth(std::max(55 * style::uiScale, ImGui::GetContentRegionAvail().x - reserve));
    if (ImGui::SliderFloat("##timeline", &position, -float(std::max(s.available, .1)), 0, "", ImGuiSliderFlags_NoInput))
        seek(-position);
    tip("Scrub through the selected receiver's audio history.\nThe spectrum and waterfall continue showing current RF, not replayed RF.\nRetuning keeps earlier audio in the history.");
    ImGui::SameLine();
    const float statusX = ImGui::GetCursorPosX();
    if (s.live) ImGui::TextColored(ImVec4(.35f, .82f, .60f, 1), "LIVE");
    else ImGui::TextColored(ImVec4(1, .75f, .35f, 1), "-%04.1fs", s.delay);
    ImGui::SameLine();
    ImGui::SetCursorPosX(statusX + 60 * style::uiScale);
    if (e.busy) ImGui::BeginDisabled();
    if (roomy) {
        if (ImGui::Button("Save 30s")) save(30);
        tip("Save up to the last 30 seconds of live received audio, including NR, before volume/mute.\nIf less history is available, save the available audio.");
        ImGui::SameLine();
        if (ImGui::Button("Save 60s")) save(60);
        tip("Save up to the last 60 seconds of live received audio.\nSaving continues in the background while you listen.");
    }
    else {
        if (ImGui::Button("Save...")) ImGui::OpenPopup("save_audio");
        if (ImGui::BeginPopup("save_audio")) {
            if (ImGui::MenuItem("Save last 30 seconds")) save(30);
            if (ImGui::MenuItem("Save last 60 seconds")) save(60);
            ImGui::EndPopup();
        }
    }
    if (e.busy) ImGui::EndDisabled();
    if (!hasAudio) ImGui::EndDisabled();
    if (!on) ImGui::TextDisabled("Audio history off");
    else if (!s.rate) ImGui::TextDisabled("Select an audio receiver (8-192 kHz); RAW bypasses rewind");
    else if (!s.head) ImGui::TextDisabled("Listening history will appear here when reception starts");
    else ImGui::TextDisabled("%s | %.1fs history | %s", streamName.c_str(), s.available,
        s.paused ? (receiving ? "Paused | RF live" : "Paused | RF stopped")
        : !s.live ? (receiving ? "Replay | RF live" : "Replay | RF stopped")
        : receiving ? "Live audio" : "Reception stopped");
    if (e.busy) { ImGui::SameLine(); ImGui::TextDisabled("| Saving..."); }
    else if (!e.error.empty()) {
        ImGui::SameLine(); ImGui::TextColored(ImVec4(1, .5f, .4f, 1), "| Save failed"); tip(e.error.c_str());
    }
    else if (!e.path.empty()) {
        ImGui::SameLine(); ImGui::TextDisabled("| Saved %.1fs", e.seconds);
#ifdef __APPLE__
        ImGui::SameLine(); if (ImGui::SmallButton("Show")) reveal();
#endif
        tip(e.path.c_str());
    }
    ImGui::PopStyleVar();
    ImGui::PopID();
}
}
