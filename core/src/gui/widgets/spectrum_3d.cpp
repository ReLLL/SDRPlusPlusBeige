#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include <gui/widgets/waterfall.h>
#include <gui/widgets/spectrum_history.h>
#include <gui/gui.h>
#include <gui/menus/display.h>
#include <gui/style.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <chrono>
#include <memory>
#include <map>

namespace ImGui {

namespace {
// Keep new state outside the exported WaterFall object. Brown plugins retain
// the exact class size and member offsets they were compiled against.
struct SpectrumState {
    bool enabled = false;
    beige::SpectrumHistory history;
    std::unique_ptr<ImDrawList> mesh;
    std::array<float, 14> params{};
    uint64_t meshRevision = UINT64_MAX;
    uint64_t paletteRevision = 0;
    uint64_t meshPaletteRevision = UINT64_MAX;
    WaterFall::SpectrumStats stats;
};

std::mutex& registryMutex() { static auto* value = new std::mutex; return *value; }
auto& registry() {
    static auto* value = new std::map<const WaterFall*, std::unique_ptr<SpectrumState>>;
    return *value;
}
SpectrumState& stateFor(const WaterFall* waterfall) {
    std::lock_guard<std::mutex> lock(registryMutex());
    auto& state = registry()[waterfall];
    if (!state) { state = std::make_unique<SpectrumState>(); }
    return *state;
}
}

void WaterFall::setSpectrum3D(bool enabled) {
    std::lock_guard<std::recursive_mutex> lock(buf_mtx);
    stateFor(this).enabled = enabled;
}

bool WaterFall::getSpectrum3D() const { return stateFor(this).enabled; }
WaterFall::SpectrumStats WaterFall::getSpectrumStats() const { return stateFor(this).stats; }

void WaterFall::captureSpectrumHistory(const float* samples, int width, int64_t timeMs) {
    if (width > 0) { stateFor(this).history.push(samples, width, timeMs); }
}
void WaterFall::spectrumPaletteChanged() { ++stateFor(this).paletteRevision; }
void WaterFall::recordSpectrumFrame(double microseconds) { stateFor(this).stats.frameUs = microseconds; }
void WaterFall::releaseSpectrumState() {
    std::lock_guard<std::mutex> lock(registryMutex());
    registry().erase(this);
}

void WaterFall::clearSpectrumHistory() {
    std::lock_guard<std::recursive_mutex> lock(buf_mtx);
    stateFor(this).history.clear();
}

void WaterFall::drawSpectrum3D() {
    auto& state = stateFor(this);
    auto& spectrumHistory = state.history;
    auto& spectrumMesh = state.mesh;
    auto& spectrumMeshParams = state.params;
    auto& spectrumMeshRevision = state.meshRevision;
    auto& spectrumPaletteRevision = state.paletteRevision;
    auto& spectrumMeshPaletteRevision = state.meshPaletteRevision;
    auto& spectrumStats = state.stats;
    ImDrawList* draw = window->DrawList;
    const ImVec2 size = fftAreaMax - fftAreaMin;
    if (size.x < 2 || size.y < 2) { return; }
    const ImU32 grid = IM_COL32(91, 96, 103, 130);
    const ImU32 text = GetColorU32(ImGuiCol_TextDisabled);
    const ImVec4 background = gui::themeManager.waterfallBg;
    const float levelRange = (std::max)(fftMax - fftMin, 1.0f);
    const float colorRange = (std::max)(waterfallMax - waterfallMin, 1.0f);
    auto project = [&](float frequency, float depth, float level) {
        const auto p = beige::projectSpectrum(frequency, depth, level);
        return ImVec2(fftAreaMin.x + p.x * size.x, fftAreaMin.y + p.y * size.y);
    };
    auto level = [&](float value) { return std::clamp((value - fftMin) / levelRange, 0.0f, 1.0f); };
    auto color = [&](float value, float depth) {
        const int paletteIndex = (int)(std::clamp((value - waterfallMin) / colorRange, 0.0f, 1.0f) * (WATERFALL_RESOLUTION - 1));
        ImVec4 c = ColorConvertU32ToFloat4(waterfallPallet[paletteIndex]);
        const float light = 1.0f - 0.32f * depth;
        c.x = c.x * light + background.x * (1.0f - light);
        c.y = c.y * light + background.y * (1.0f - light);
        c.z = c.z * light + background.z * (1.0f - light);
        c.w = 1.0f;
        return ColorConvertFloat4ToU32(c);
    };

    const std::array<float, 14> params = {fftAreaMin.x, fftAreaMin.y, fftAreaMax.x, fftAreaMax.y,
        fftMin, fftMax, waterfallMin, waterfallMax, style::uiScale, background.x, background.y,
        background.z, displaymenu::showFFT ? 1.0f : 0.0f, GetFontSize()};
    ImDrawList* output = draw;
    if (!spectrumMesh || spectrumMeshRevision != spectrumHistory.revision() ||
        spectrumMeshPaletteRevision != spectrumPaletteRevision || spectrumMeshParams != params) {
        const auto buildStart = std::chrono::steady_clock::now();
        if (!spectrumMesh) { spectrumMesh = std::make_unique<ImDrawList>(GetDrawListSharedData()); }
        draw = spectrumMesh.get();
        draw->_ResetForNewFrame();
        draw->PushTextureID(GetIO().Fonts->TexID);
        draw->PushClipRect(fftAreaMin, fftAreaMax, false);
        const ImVec2 floor[] = {project(0, 0, 0), project(1, 0, 0), project(1, 1, 0), project(0, 1, 0)};
        draw->AddConvexPolyFilled(floor, 4, IM_COL32(22, 25, 30, 255));
        for (int i = 0; i <= 10; ++i) {
            const float f = i / 10.0f;
            draw->AddLine(project(f, 0, 0), project(f, 1, 0), grid);
        }
        for (int i = 0; i <= 6; ++i) {
            const float depth = i / 6.0f;
            draw->AddLine(project(0, depth, 0), project(1, depth, 0), grid);
        }

        // Painter's order: oldest strips first, newest strips last. Each vertex's
        // color comes from the same dB-to-palette mapping as the 2D waterfall.
        if (displaymenu::showFFT && spectrumHistory.size() > 0) {
            constexpr std::size_t maxDrawRows = 48;
            const std::size_t count = (std::min)(spectrumHistory.size(), maxDrawRows);
            auto rowIndex = [&](std::size_t row) {
                return count > 1 ? row * (spectrumHistory.size() - 1) / (count - 1) : std::size_t{0};
            };
            auto depthAt = [&](std::size_t age) {
                return std::clamp((float)spectrumHistory.ageMs(age) / beige::SpectrumHistory::durationMs, 0.0f, 1.0f);
            };
            const ImVec2 uv = GetFontTexUvWhitePixel();
            for (std::size_t r = count; r-- > 1;) {
                const auto backIndex = rowIndex(r);
                const auto frontIndex = rowIndex(r - 1);
                const auto& back = spectrumHistory.row(backIndex);
                const auto& front = spectrumHistory.row(frontIndex);
                const float backDepth = depthAt(backIndex);
                const float frontDepth = depthAt(frontIndex);
                draw->PrimReserve(6 * (beige::SpectrumHistory::bins - 1), 4 * (beige::SpectrumHistory::bins - 1));
                for (std::size_t bin = 0; bin + 1 < beige::SpectrumHistory::bins; ++bin) {
                    const float x0 = (float)bin / (beige::SpectrumHistory::bins - 1);
                    const float x1 = (float)(bin + 1) / (beige::SpectrumHistory::bins - 1);
                    const ImDrawIdx base = (ImDrawIdx)draw->_VtxCurrentIdx;
                    draw->PrimWriteIdx(base); draw->PrimWriteIdx(base + 1); draw->PrimWriteIdx(base + 2);
                    draw->PrimWriteIdx(base); draw->PrimWriteIdx(base + 2); draw->PrimWriteIdx(base + 3);
                    draw->PrimWriteVtx(project(x0, backDepth, level(back[bin])), uv, color(back[bin], backDepth));
                    draw->PrimWriteVtx(project(x1, backDepth, level(back[bin + 1])), uv, color(back[bin + 1], backDepth));
                    draw->PrimWriteVtx(project(x1, frontDepth, level(front[bin + 1])), uv, color(front[bin + 1], frontDepth));
                    draw->PrimWriteVtx(project(x0, frontDepth, level(front[bin])), uv, color(front[bin], frontDepth));
                }
                if (r % 4 == 0) {
                    std::array<ImVec2, beige::SpectrumHistory::bins> ridge;
                    for (std::size_t bin = 0; bin < ridge.size(); ++bin) {
                        ridge[bin] = project((float)bin / (ridge.size() - 1), backDepth, level(back[bin]));
                    }
                    draw->AddPolyline(ridge.data(), (int)ridge.size(), IM_COL32(240, 240, 240, 38), 0, 1.0f);
                }
            }
            // Close the front face down to the floor so even one FFT is a real profile.
            const auto& newest = spectrumHistory.row(0);
            for (std::size_t bin = 0; bin + 1 < newest.size(); ++bin) {
                const float x0 = (float)bin / (newest.size() - 1);
                const float x1 = (float)(bin + 1) / (newest.size() - 1);
                const ImVec2 face[] = {project(x0, 0, 0), project(x1, 0, 0), project(x1, 0, level(newest[bin + 1])), project(x0, 0, level(newest[bin]))};
                draw->AddConvexPolyFilled(face, 4, color((std::max)(newest[bin], newest[bin + 1]), 0));
                draw->AddLine(face[3], face[2], IM_COL32(250, 249, 236, 230), style::uiScale);
            }
        }
        draw->AddText(fftAreaMin + ImVec2(8, 8) * style::uiScale, text, "3D SPECTRUM / TIME");
        draw->PopClipRect();
        draw->PopTextureID();
        spectrumMeshRevision = spectrumHistory.revision();
        spectrumMeshPaletteRevision = spectrumPaletteRevision;
        spectrumMeshParams = params;
        spectrumStats.meshBuildUs = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - buildStart).count();
        ++spectrumStats.meshBuilds;
    }
    draw = output;
    draw->PushClipRect(fftAreaMin, fftAreaMax, true);
    const int vertexCount = spectrumMesh->VtxBuffer.Size;
    const int indexCount = spectrumMesh->IdxBuffer.Size;
    spectrumStats.vertices = vertexCount;
    spectrumStats.indices = indexCount;
    spectrumStats.historyRows = spectrumHistory.size();
    draw->PrimReserve(indexCount, vertexCount);
    const unsigned int base = draw->_VtxCurrentIdx;
    for (int i = 0; i < indexCount; ++i) { draw->_IdxWritePtr[i] = (ImDrawIdx)(base + spectrumMesh->IdxBuffer[i]); }
    memcpy(draw->_VtxWritePtr, spectrumMesh->VtxBuffer.Data, vertexCount * sizeof(ImDrawVert));
    draw->_IdxWritePtr += indexCount;
    draw->_VtxWritePtr += vertexCount;
    draw->_VtxCurrentIdx += vertexCount;
    draw->PopClipRect();

    // dB scale is on the front plane. Frequency ruler below remains full-width
    // and shared with standard tuning, VFOs, bookmarks and the 2D waterfall.
    for (int i = 0; i <= 4; ++i) {
        const float value = fftMin + levelRange * i / 4.0f;
        const ImVec2 p = project(0, 0, i / 4.0f);
        char label[24];
        snprintf(label, sizeof(label), "%.0f", value);
        const ImVec2 labelSize = CalcTextSize(label);
        draw->AddText(ImVec2(fftAreaMin.x - labelSize.x - 8 * style::uiScale, p.y - labelSize.y * 0.5f), text, label);
        draw->AddLine(p, p - ImVec2(4 * style::uiScale, 0), text);
    }
}

} // namespace ImGui
