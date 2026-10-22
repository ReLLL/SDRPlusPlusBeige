#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <dsp/types.h>

namespace dsp {
class NeuralClarity {
public:
    enum : unsigned { Detail = 1, Smooth = 2, Lift = 4, All = 7 };

    void reset() {
        previous_.fill(0);
        hold_ = 0;
        speech_ = smoothing_ = 0;
    }

    void frame(const complex_t* dry, complex_t* wet, unsigned options) {
        if (!options && smoothing_ == 0) { reset(); return; }
        double input = 0, output = 0;
        for (int i = 4; i <= 128; ++i) {
            input += power(dry[i]); output += power(wet[i]);
        }
        if (output > 1e-10 && output > .025 * input) hold_ = 8;
        else if (hold_) --hold_;
        speech_ += (hold_ ? .65f : .25f) * ((hold_ ? 1.f : 0.f) - speech_);
        smoothing_ += .25f * ((options & Smooth ? 1.f : 0.f) - smoothing_);
        if (!(options & Smooth) && smoothing_ < 1e-6f) smoothing_ = 0;
        for (int i = 0; i < 257; ++i) {
            const double a = power(dry[i]);
            const float gain = a > 1e-18 ? float(std::sqrt(power(wet[i]) / a)) : 0;
            const float current = (std::min)(gain, 1.f);
            const float retained = (std::max)(current, previous_[i] * .8f);
            previous_[i] = retained;
            // A short, bounded gain floor around model-recognized speech.
            const float mix = (std::min)(.35f, (retained - current) / (1.00001f - current))
                * speech_ * smoothing_;
            if (mix > 0) {
                complex_t original = dry[i];
                wet[i] = wet[i] * (1.f - mix) + original * mix;
            }
        }
    }

    float speech() const { return speech_; }

private:
    static double power(complex_t value) { return double(value.re)*value.re + double(value.im)*value.im; }
    std::array<float,257> previous_{};
    int hold_ = 0;
    float speech_ = 0, smoothing_ = 0;
};
}
