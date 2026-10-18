#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include <gui/widgets/waterfall.h>
#include <gui/widgets/spectrum_history.h>
#include <gui/widgets/spectrum_mesh.h>
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
#include <vector>

namespace ImGui {

namespace {
// Keep new state outside the exported WaterFall object. Brown plugins retain
// the exact class size and member offsets they were compiled against.
struct SpectrumState {
    struct Position { float frequency, depth, level, offsetY; bool travels; };
    bool enabled = false;
    beige::SpectrumHistory history;
    std::unique_ptr<ImDrawList> mesh;
    std::vector<Position> positions;
    std::array<float, 14> params{};
    uint64_t meshRevision = UINT64_MAX;
    uint64_t paletteRevision = 0;
    uint64_t meshPaletteRevision = UINT64_MAX;
    double displayTime = 0;
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

        // Keep all time slices. Changing a subsampled row selection made ridges
        // jump as history filled. Share row vertices to afford a detailed front.
        if (displaymenu::showFFT && spectrumHistory.size() > 0) {
            const std::size_t count = spectrumHistory.size();
            auto depthAt = [&](std::size_t age) {
                return (float)spectrumHistory.depthAt(age, spectrumHistory.latestTime());
            };
            const ImVec2 uv = GetFontTexUvWhitePixel();
            std::array<std::size_t, beige::SpectrumHistory::capacity> columns{}, starts{};
            std::size_t vertices = 0, indices = 0;
            for (std::size_t r = 0; r < count; ++r) {
                columns[r] = beige::spectrumColumns(size.x, r);
                starts[r] = vertices;
                vertices += columns[r] + 1;
                if (r) { indices += 3 * (columns[r - 1] + columns[r]); }
            }
            const auto frontColumns = columns[0];
            const std::size_t bottom = vertices, outline = bottom + frontColumns + 1;
            vertices += 3 * (frontColumns + 1);
            indices += 12 * frontColumns;
            state.positions.assign(draw->VtxBuffer.Size, {0, -1, 0, 0, false});
            state.positions.reserve(state.positions.size() + vertices);
            draw->PrimReserve((int)indices, (int)vertices);
            const unsigned int base = draw->_VtxCurrentIdx;
            auto triangle = [&](std::size_t a, std::size_t b, std::size_t c) {
                draw->PrimWriteIdx((ImDrawIdx)(base + a));
                draw->PrimWriteIdx((ImDrawIdx)(base + b));
                draw->PrimWriteIdx((ImDrawIdx)(base + c));
            };
            auto vertex = [&](float f, float depth, float height, ImU32 tint, float offsetY = 0, bool travels = false) {
                draw->PrimWriteVtx(project(f, depth, height) + ImVec2(0, offsetY), uv, tint);
                state.positions.push_back({f, depth, height, offsetY, travels});
            };
            auto sample = [&](std::size_t row, std::size_t bin) {
                const auto& values = spectrumHistory.row(row);
                const std::size_t first = (std::min)(bin * values.size() / columns[row], values.size() - 1);
                const std::size_t end = (std::min)((std::max)(first + 1, (bin + 1) * values.size() / columns[row]), values.size());
                return *std::max_element(values.begin() + first, values.begin() + end);
            };
            for (std::size_t r = 0; r < count; ++r) {
                const float depth = depthAt(r);
                for (std::size_t bin = 0; bin <= columns[r]; ++bin) {
                    const float value = sample(r, bin);
                    vertex((float)bin / columns[r], depth, level(value), color(value, depth), 0, r > 0);
                }
            }
            // Oldest strips first, with no index-based ridge lines to shimmer.
            for (std::size_t r = count; r-- > 1;) {
                beige::connectSpectrumRows(starts[r - 1], columns[r - 1], starts[r], columns[r], triangle);
            }
            for (std::size_t bin = 0; bin <= frontColumns; ++bin) {
                vertex((float)bin / frontColumns, 0, 0, color(sample(0, bin), 0));
            }
            for (std::size_t bin = 0; bin <= frontColumns; ++bin) {
                const float height = level(sample(0, bin));
                vertex((float)bin / frontColumns, 0, height, IM_COL32(250, 249, 236, 230), -0.5f * style::uiScale);
                vertex((float)bin / frontColumns, 0, height, IM_COL32(250, 249, 236, 230), 0.5f * style::uiScale);
            }
            for (std::size_t bin = 0; bin < frontColumns; ++bin) {
                triangle(bottom + bin, bottom + bin + 1, bin + 1);
                triangle(bottom + bin, bin + 1, bin);
                triangle(outline + 2 * bin, outline + 2 * bin + 1, outline + 2 * bin + 3);
                triangle(outline + 2 * bin, outline + 2 * bin + 3, outline + 2 * bin + 2);
            }
        }
        else { state.positions.clear(); }
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
    // Reproject cached vertices at display cadence. FFT cadence controls new
    // data only, never Z movement. Keep the last displayed time when stopped.
    const double now = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    if (!spectrumHistory.size()) { state.displayTime = 0; }
    else if (gui::mainWindow.isPlaying()) { state.displayTime = now; }
    else { state.displayTime = (std::max)(state.displayTime, (double)spectrumHistory.latestTime()); }
    const float advance = (float)((state.displayTime - spectrumHistory.latestTime()) / beige::SpectrumHistory::durationMs);
    for (std::size_t i = 0; i < state.positions.size(); ++i) {
        const auto& p = state.positions[i];
        if (p.depth < 0) { continue; }
        const float depth = p.travels ? std::clamp(p.depth + advance, 0.0f, 1.0f) : p.depth;
        const float fade = std::clamp((1.0f - depth) / 0.06f, 0.0f, 1.0f);
        draw->_VtxWritePtr[i].pos = project(p.frequency, depth, p.level * fade) + ImVec2(0, p.offsetY);
    }
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
