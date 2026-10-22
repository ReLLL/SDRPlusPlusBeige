#pragma once
#include <algorithm>
#include <cmath>
#include <dsp/types.h>

namespace dsp {
// Estimate attenuation in bins retained by the model. No full-noise RMS matching.
class NeuralVoiceLevel {
public:
    void reset() { gain_ = 1; dry_ = wet_ = 0; }
    float gain() const { return gain_; }

    void update(const complex_t* dry, const complex_t* wet) {
        double weightedDry = 0, weightedWet = 0;
        for (int i = 4; i <= 128; ++i) { // 125-4000 Hz at the model's 16 kHz rate.
            const double a = double(dry[i].re)*dry[i].re + double(dry[i].im)*dry[i].im;
            const double b = double(wet[i].re)*wet[i].re + double(wet[i].im)*wet[i].im;
            const double weight = a > 1e-18 ? (std::min)(b / a, 1.) : 0;
            if (weight < .01) continue;
            weightedDry += a * weight;
            weightedWet += b * weight;
        }
        // Hold through rejected noise and silence rather than pumping its level.
        if (weightedDry < 1e-12 || weightedWet < 1e-12) return;
        dry_ += .15 * (weightedDry - dry_);
        wet_ += .15 * (weightedWet - wet_);
        const float desired = std::clamp(float(std::sqrt(dry_ / wet_)), 1.f, 4.f);
        // Reduce gain quickly for a new loud voice; recover weak speech gradually.
        gain_ += (desired < gain_ ? .15f : .08f) * (desired - gain_);
    }
private:
    double dry_ = 0, wet_ = 0;
    float gain_ = 1;
};
}
