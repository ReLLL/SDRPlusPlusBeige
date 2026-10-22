#pragma once
#include <array>
#include <cmath>
#include <numeric>
#include <stdexcept>
#include <vector>
#include <dsp/types.h>
#include <volk/volk.h>

namespace dsp {
// Small causal polyphase FIR for speech. Storage does not grow with input size.
template<typename T> class AudioResampler {
public:
    void configure(int inputRate, int outputRate) {
        if (inputRate < 8000 || outputRate < 8000 || inputRate > 192000 || outputRate > 192000)
            throw std::invalid_argument("Unsupported audio rate");
        const int divisor = std::gcd(inputRate, outputRate);
        interpolation_ = outputRate / divisor;
        decimation_ = inputRate / divisor;
        if (interpolation_ > 2048) throw std::invalid_argument("Unsupported audio ratio");
        coefficients_.resize(interpolation_);
        const double cutoff = .45 * (std::min)(1., double(outputRate) / inputRate);
        for (int phase = 0; phase < interpolation_; ++phase) {
            double sum = 0;
            for (int i = 0; i < taps; ++i) {
                const double distance = (taps - 1) / 2. - i + double(phase) / interpolation_;
                const double argument = 2 * cutoff * distance;
                const double sinc = std::abs(argument) < 1e-12 ? 1 : std::sin(pi * argument) / (pi * argument);
                const double window = .42 - .5 * std::cos(2 * pi * i / (taps - 1))
                    + .08 * std::cos(4 * pi * i / (taps - 1));
                coefficients_[phase][i] = float(2 * cutoff * sinc * window);
                sum += coefficients_[phase][i];
            }
            for (auto& value : coefficients_[phase]) value /= sum;
        }
        reset();
    }

    void reset() { history_.fill(T{}); write_ = phase_ = 0; inputIndex_ = nextIndex_ = 0; }

    template<typename Consumer> void push(T input, Consumer consume) {
        history_[write_] = history_[write_ + taps] = input;
        write_ = (write_ + 1) % taps;
        while (nextIndex_ <= inputIndex_) {
            T output{};
            if constexpr (std::is_same_v<T, float>)
                volk_32f_x2_dot_prod_32f(&output, history_.data() + write_, coefficients_[phase_].data(), taps);
            else
                volk_32fc_32f_dot_prod_32fc(reinterpret_cast<lv_32fc_t*>(&output),
                    reinterpret_cast<const lv_32fc_t*>(history_.data() + write_), coefficients_[phase_].data(), taps);
            consume(output);
            phase_ += decimation_;
            nextIndex_ += phase_ / interpolation_;
            phase_ %= interpolation_;
        }
        ++inputIndex_;
    }

private:
    static constexpr int taps = 64;
    static constexpr double pi = 3.14159265358979323846;
    std::array<T, taps * 2> history_{};
    std::vector<std::array<float, taps>> coefficients_;
    int interpolation_ = 1, decimation_ = 1, phase_ = 0, write_ = 0;
    int64_t inputIndex_ = 0, nextIndex_ = 0;
};
}
