#pragma once
#include "store.h"
#include <algorithm>
#include <functional>
#include <stdexcept>

namespace beige::iqreplay {
// Non-overlapping FFT windows, matching IQFrontEnd's keep/skip geometry. Each
// frame carries its actual center sample, independent of thread/render timing.
class Framer {
public:
    using Callback = std::function<void(dsp::complex_t*, int, const Stamp&)>;
    Framer(size_t keep, size_t interval) : window(keep), interval(interval) {
        if (!keep || interval < keep) throw std::invalid_argument("Invalid IQ FFT geometry");
    }
    void push(const dsp::complex_t* data, size_t count, Stamp first, const Callback& callback) {
        if (first.epoch != epoch || (first && first.sample != expected)) {
            position = 0; epoch = first.epoch;
        }
        expected = first.sample + count;
        while (count) {
            if (position == 0) start = first;
            const size_t n = std::min(count, (position < window.size() ? window.size() : interval) - position);
            if (position < window.size()) std::copy_n(data, n, window.data() + position);
            position += n; first.sample += n; data += n; count -= n;
            if (position == window.size()) {
                auto center = start; center.sample += window.size() / 2;
                callback(window.data(), int(window.size()), center);
            }
            if (position == interval) position = 0;
        }
    }
private:
    std::vector<dsp::complex_t> window;
    size_t interval, position = 0;
    uint64_t epoch = 0, expected = 0;
    Stamp start;
};
}
