#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace beige {

// Access under WaterFall::buf_mtx. Fixed storage keeps FFT ingestion allocation-free.
class SpectrumHistory {
public:
    static constexpr std::size_t bins = 192;
    static constexpr std::size_t capacity = 64;
    static constexpr int64_t intervalMs = 100;
    static constexpr int64_t durationMs = (capacity - 1) * intervalMs;
    using Row = std::array<float, bins>;

    void clear() {
        ++revision_;
        count_ = 0;
        head_ = 0;
        bucketStarted_ = 0;
    }

    void push(const float* samples, std::size_t width, int64_t now) {
        if (!samples || width == 0) { return; }
        if (count_ == 0 || now < times_[head_] || now - times_[head_] > durationMs) {
            clear();
        }
        if (count_ == 0) {
            count_ = 1;
            bucketStarted_ = now;
        }
        else if (now - bucketStarted_ >= intervalMs) {
            head_ = (head_ + 1) % capacity;
            count_ = (std::min)(count_ + 1, capacity);
            bucketStarted_ = now;
        }
        times_[head_] = now;
        ++revision_;
        while (count_ > 1 && ageMs(count_ - 1) > durationMs) { --count_; }
        for (std::size_t bin = 0; bin < bins; ++bin) {
            const std::size_t first = bin * width / bins;
            const std::size_t end = (std::min)(width, (std::max)(first + 1, (bin + 1) * width / bins));
            float peak = -200.0f;
            for (std::size_t i = first; i < end; ++i) {
                if (std::isfinite(samples[i])) { peak = (std::max)(peak, samples[i]); }
            }
            rows_[head_][bin] = peak;
        }
    }

    std::size_t size() const { return count_; }
    uint64_t revision() const { return revision_; }
    const Row& row(std::size_t age) const { return rows_[index(age)]; }
    int64_t ageMs(std::size_t age) const { return times_[head_] - times_[index(age)]; }

private:
    std::size_t index(std::size_t age) const { return (head_ + capacity - age) % capacity; }
    std::array<Row, capacity> rows_{};
    std::array<int64_t, capacity> times_{};
    std::size_t head_ = 0;
    std::size_t count_ = 0;
    int64_t bucketStarted_ = 0;
    uint64_t revision_ = 0;
};

struct SpectrumPoint { float x; float y; };

// Newest (depth=0) spans the front frequency ruler; older rows recede toward the back.
inline SpectrumPoint projectSpectrum(float frequency, float depth, float level) {
    const float scale = 1.0f / (1.0f + 0.62f * depth);
    return { 0.5f + (frequency - 0.5f + 0.08f * depth) * scale,
             1.0f - 0.50f * depth - 0.48f * level * scale };
}

} // namespace beige
