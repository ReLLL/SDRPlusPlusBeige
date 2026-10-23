#pragma once
#include "store.h"
#include <algorithm>
#include <cmath>

namespace beige::iqreplay {
// This is indexed exactly like WaterFall's raw FFT ring. The displayed snapshot
// is separate: input events refer to pixels already shown, not a newer FFT head.
class Rows {
public:
    void resize(int oldHead, int oldHeight, int height) {
        height = std::clamp(height, 0, 32768);
        std::vector<Stamp> next(size_t(height), Stamp{});
        if (oldHead >= 0 && oldHeight > 0 && oldHead < oldHeight && size_t(oldHeight) == ring.size())
            for (int i = 0; i < std::min(height, oldHeight); ++i) next[i] = ring[(oldHead + i) % oldHeight];
        ring.swap(next);
    }
    void put(int index, int height, const Stamp& stamp) {
        if (height <= 0 || height > 32768 || index < 0 || index >= height) return;
        if (ring.size() != size_t(height)) ring.assign(size_t(height), Stamp{});
        ring[size_t(index)] = stamp;
    }
    std::vector<Stamp> snapshot(int head, int filled, int height) const {
        if (height <= 0 || height > 32768 || head < 0 || head >= height || ring.size() != size_t(height)) return {};
        std::vector<Stamp> result(size_t(height), Stamp{});
        for (int i = 0; i < std::clamp(filled, 0, height); ++i) result[i] = ring[(head + i) % height];
        return result;
    }
    void clear() { ring.clear(); }
    static Stamp point(const std::vector<Stamp>& display, double y) {
        if (!std::isfinite(y) || y < 0 || y >= double(display.size())) return {};
        return display[size_t(std::floor(y))];
    }
private:
    std::vector<Stamp> ring;
};
}
