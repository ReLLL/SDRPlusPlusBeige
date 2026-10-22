#pragma once
#include <dsp/types.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <vector>

namespace beige::rewind {
struct State {
    int rate = 0;
    uint64_t head = 0, epoch = 0;
    double available = 0, delay = 0;
    size_t bytes = 0;
    bool live = true, paused = false;
};

struct Clip {
    int rate = 0;
    std::vector<dsp::stereo_t> samples;
};

// One audio writer; controls and export snapshots use short bounded locks.
// Five extra seconds protect click-time exports while the worker starts.
class Buffer {
public:
    void configure(int rate) {
        if (rate < 8000 || rate > 192000) rate = 0;
        std::vector<dsp::stereo_t> next(size_t(rate) * 65);
        std::lock_guard<std::mutex> lock(mutex);
        ring.swap(next);
        sampleRate = rate;
        head = cursor = 0;
        ++epoch;
        live = true;
        paused = false;
        fading = 0;
        last = {};
    }

    State state() const {
        std::lock_guard<std::mutex> lock(mutex);
        return {sampleRate, head, epoch, sampleRate ? double(head - oldest()) / sampleRate : 0,
            sampleRate && !live ? double(head - std::max(cursor, oldest())) / sampleRate : 0,
            ring.size() * sizeof(dsp::stereo_t), live, paused};
    }

    void seek(double seconds) {
        if (!std::isfinite(seconds)) return;
        std::lock_guard<std::mutex> lock(mutex);
        if (!sampleRate || !head) return;
        const auto frames = uint64_t(std::clamp(seconds, 0.0, 60.0) * sampleRate);
        cursor = head - std::min(frames, head - oldest());
        live = frames == 0;
        if (live) paused = false;
        transition();
    }

    void setPaused(bool value) {
        std::lock_guard<std::mutex> lock(mutex);
        if (value && live) { cursor = head; live = false; }
        paused = value;
        transition();
    }

    void goLive() {
        std::lock_guard<std::mutex> lock(mutex);
        live = true;
        paused = false;
        cursor = head;
        transition();
    }

    void process(const dsp::stereo_t* input, dsp::stereo_t* output, size_t count) {
        std::lock_guard<std::mutex> lock(mutex);
        if (ring.empty()) {
            std::memcpy(output, input, count * sizeof(*output));
            return;
        }
        const size_t keep = std::min(count, ring.size());
        const size_t skipped = count - keep;
        const size_t index = (head + skipped) % ring.size();
        const size_t first = std::min(keep, ring.size() - index);
        std::memcpy(ring.data() + index, input + skipped, first * sizeof(*input));
        std::memcpy(ring.data(), input + skipped + first, (keep - first) * sizeof(*input));
        head += count;
        render(output, count, input);
    }

    void renderStopped(dsp::stereo_t* output, size_t count) {
        std::lock_guard<std::mutex> lock(mutex);
        render(output, count, nullptr);
    }

    Clip snapshot(const State& at, double seconds) const {
        Clip result;
        if (at.rate <= 0 || !std::isfinite(seconds) || seconds <= 0) return result;
        result.samples.resize(size_t(std::min(seconds, 60.0) * at.rate));
        std::lock_guard<std::mutex> lock(mutex);
        if (at.epoch != epoch || ring.empty()) { result.samples.clear(); return result; }
        const uint64_t end = std::min(at.head, head);
        const uint64_t start = std::max(end - std::min<uint64_t>(end, result.samples.size()),
            head > ring.size() ? head - ring.size() : 0);
        if (start >= end) { result.samples.clear(); return result; }
        result.rate = sampleRate;
        result.samples.resize(size_t(end - start));
        const size_t index = start % ring.size();
        const size_t first = std::min(result.samples.size(), ring.size() - index);
        std::memcpy(result.samples.data(), ring.data() + index, first * sizeof(dsp::stereo_t));
        std::memcpy(result.samples.data() + first, ring.data(),
            (result.samples.size() - first) * sizeof(dsp::stereo_t));
        return result;
    }

private:
    uint64_t oldest() const { return head - std::min<uint64_t>(head, uint64_t(sampleRate) * 60); }
    void transition() { fading = std::max(1, sampleRate / 200); fadeFrom = last; }

    void render(dsp::stereo_t* output, size_t count, const dsp::stereo_t* input) {
        if (live && input) std::memcpy(output, input, count * sizeof(*output));
        else {
            cursor = std::max(cursor, oldest());
            for (size_t i = 0; i < count; ++i) {
                if (!paused && !live && cursor < head && !ring.empty()) output[i] = ring[cursor++ % ring.size()];
                else output[i] = {};
            }
            if (!input && !live && cursor == head) paused = true;
        }
        const int length = std::max(1, sampleRate / 200);
        for (size_t i = 0; i < count && fading > 0; ++i, --fading) {
            const float mix = float(length - fading + 1) / length;
            output[i].l = fadeFrom.l * (1 - mix) + output[i].l * mix;
            output[i].r = fadeFrom.r * (1 - mix) + output[i].r * mix;
        }
        if (count) last = output[count - 1];
        if (live) cursor = head;
    }

    mutable std::mutex mutex;
    std::vector<dsp::stereo_t> ring;
    int sampleRate = 0, fading = 0;
    uint64_t head = 0, cursor = 0, epoch = 0;
    bool live = true, paused = false;
    dsp::stereo_t last{}, fadeFrom{};
};
}
