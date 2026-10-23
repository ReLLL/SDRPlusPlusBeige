#pragma once
#include "store.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace beige::iqreplay {
struct Retention {
    uint64_t capacity, minimum;
    double seconds;
    static Retention plan(double rate, double fftRate, int rows) {
        if (!std::isfinite(rate) || rate <= 0 || !std::isfinite(fftRate) || fftRate <= 0)
            throw std::invalid_argument("Invalid IQ sample rate or waterfall speed.");
        rows = std::clamp(rows, 1, 32768);
        const double period = std::max(1.0, std::round(rate / fftRate));
        const double capacity = (rows + 8) * period + 2 * rate;
        if (!std::isfinite(capacity) || capacity > double(std::numeric_limits<int64_t>::max() / sizeof(dsp::complex_t)))
            throw std::invalid_argument("The waterfall IQ buffer is too large.");
        return {uint64_t(std::ceil(capacity)), uint64_t(std::ceil(rows * period + rate)), capacity / rate};
    }
    bool replace(uint64_t current) const {
        // The margin absorbs small layout differences without deleting history.
        return current < minimum || double(current) > double(capacity) * 1.25;
    }
};
inline bool retainedPoint(const StoreState& b, const Stamp& at, double frequency) {
    return b.ready && at && at.epoch == b.epoch && at.rate == b.rate && at.center == b.center &&
        at.sample >= b.oldest() && at.sample < b.captured && std::isfinite(frequency) &&
        frequency >= b.center - b.rate / 2 && frequency < b.center + b.rate / 2;
}
}
