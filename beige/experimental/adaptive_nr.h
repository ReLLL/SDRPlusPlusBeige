#pragma once

// Experimental only. Oz rejected the 1.3.15 live sound/latency on October 22.
// The active receiver uses ClassicNR; synthetic metrics cannot qualify this path.

#include <utils/complex_fft.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

namespace dsp {

// Complex IQ, phase-preserving spectral suppression. No speech model: CW and
// digital modulation must survive too. Fixed memory, independent of run length.
class AdaptiveNR {
public:
    void configure(int sampleRate, bool preferApple) {
        int n = 256;
        // About 5-11 ms of RF; bounded at high SDR rates.
        while (n < sampleRate / 100 && n < 32768) n *= 2;
        size_ = n;
        hop_ = n / 2;
        forward_ = std::make_unique<fft::ComplexFFT>(n, false, preferApple);
        inverse_ = std::make_unique<fft::ComplexFFT>(n, true, preferApple);
        window_.resize(n);
        for (int i = 0; i < n; ++i) window_[i] = std::sin(3.141592653589793 * (i + 0.5) / n);
        input_.resize(n);
        spectrum_.resize(n);
        overlap_.resize(n);
        output_.resize(hop_);
        power_.resize(n);
        smooth_.resize(n);
        noise_.resize(n);
        gains_.resize(n);
        target_.resize(n);
        previous_.resize(n);
        floor_.resize(n);
        tileScratch_.resize(128);
        // Time constants are in seconds, independent of sample rate/FFT size.
        const float seconds = float(hop_) / sampleRate;
        smoothing_ = std::exp(-seconds / 0.035f);
        noiseRise_ = std::exp(-seconds / 0.6f);
        noiseFall_ = std::exp(-seconds / 0.08f);
        gainRelease_ = std::exp(-seconds / 0.045f);
        reset();
    }

    void reset() {
        std::fill(input_.begin(), input_.end(), complex_t{0, 0});
        std::fill(overlap_.begin(), overlap_.end(), complex_t{0, 0});
        std::fill(output_.begin(), output_.end(), complex_t{0, 0});
        std::fill(smooth_.begin(), smooth_.end(), 0);
        std::fill(noise_.begin(), noise_.end(), 0);
        std::fill(gains_.begin(), gains_.end(), 1);
        std::fill(previous_.begin(), previous_.end(), 0);
        fill_ = 0;
        read_ = 0;
        frames_ = 0;
    }

    void setReduction(float db) { reduction_ = std::clamp(db, 0.0f, 24.0f); }
    bool usesApple() const { return forward_ && forward_->usesApple(); }
    int latency() const { return size_; }
    int fftSize() const { return size_; }

    // Exact 1:1 sample count for every input partition. One FFT-length startup
    // delay, zero-padded history; no 280 ms initial noise-only capture or bursts.
    void process(const complex_t* input, int count, complex_t* output, bool hold = false) {
        for (int i = 0; i < count; ++i) {
            complex_t value = input[i];
            if (!std::isfinite(value.re) || !std::isfinite(value.im)) value = {0, 0};
            input_[hop_ + fill_] = value;
            output[i] = output_[read_++];
            if (++fill_ == hop_) {
                processFrame(hold);
                std::copy(input_.begin() + hop_, input_.end(), input_.begin());
                fill_ = read_ = 0;
            }
        }
    }

private:
    void processFrame(bool hold) {
        for (int i = 0; i < size_; ++i) spectrum_[i] = input_[i] * window_[i];
        forward_->execute(spectrum_.data());
        forward_->power(spectrum_.data(), power_.data());
        for (int i = 0; i < size_; ++i) {
            // Bound pathological source values before recursive state gets NaNs.
            power_[i] = std::clamp(power_[i], 1e-20f, 1e20f);
            smooth_[i] = frames_ ? smoothing_ * smooth_[i] + (1 - smoothing_) * power_[i] : power_[i];
        }

        // Two-scale lower quartiles exclude persistent carriers at startup and
        // protect occupied SSB/AM channels from contaminating the local floor.
        if (frames_ % 4 == 0) {
        for (int start = 0; start < size_; start += 128) {
            std::copy_n(smooth_.begin() + start, 128, tileScratch_.begin());
            std::nth_element(tileScratch_.begin(), tileScratch_.begin() + 32, tileScratch_.end());
            std::fill_n(target_.begin() + start, 128, tileScratch_[32] * 2.0f);
        }
        for (int start = -16; start < size_; start += 16) {
            for (int j = 0; j < 32; ++j) tileScratch_[j] = smooth_[(start + j + size_) % size_];
            std::nth_element(tileScratch_.begin(), tileScratch_.begin() + 8, tileScratch_.begin() + 32);
            const float estimate = (std::min)(tileScratch_[8] * 1.5f, target_[(start + 16 + size_) % size_]);
            for (int j = 8; j < 24; ++j) floor_[(start + j + size_) % size_] = estimate;
        }
        }
        const float minimumGain = std::pow(10.0f, -reduction_ / 20.0f);
        for (int i = 0; i < size_; ++i) {
            if (!frames_) noise_[i] = floor_[i];
            else if (!hold) {
                // Local quantile excludes stable narrow signals, unlike a
                // per-bin running mean that gradually learns them as noise.
                float alpha = floor_[i] > noise_[i] ? noiseRise_ : noiseFall_;
                noise_[i] = alpha * noise_[i] + (1 - alpha) * floor_[i];
            }
            const float noise = (std::max)(noise_[i], 1e-20f);
            const float posterior = (std::min)(power_[i] / noise, 10000.0f);
            const float prior = 0.94f * previous_[i] / noise + 0.06f * (std::max)(posterior - 1.0f, 0.0f);
            float gain = prior / (prior + 1.0f);
            // Open rapidly for coherent/strong signals, preserving transients.
            const float presence = std::clamp((smooth_[i] / noise - 2.0f) / 6.0f, 0.0f, 1.0f);
            gain = (std::max)(gain, presence);
            target_[i] = std::clamp(gain, minimumGain, 1.0f);
        }
        for (int i = 0; i < size_; ++i) {
            // Only lift a gain with its neighbors: never smear an isolated
            // wanted peak down into the noise. This reduces musical noise.
            const float neighbors = 0.25f * target_[(i + size_ - 1) % size_] +
                0.5f * target_[i] + 0.25f * target_[(i + 1) % size_];
            const float target = (std::max)(target_[i], neighbors);
            gains_[i] = target > gains_[i] ? target : gainRelease_ * gains_[i] + (1 - gainRelease_) * target;
            previous_[i] = power_[i] * gains_[i] * gains_[i];
        }
        forward_->applyGain(spectrum_.data(), gains_.data());
        inverse_->execute(spectrum_.data());
        // Sine windows at 50% overlap have exactly unit squared overlap.
        for (int i = 0; i < size_; ++i) overlap_[i] += spectrum_[i] * window_[i];
        std::copy_n(overlap_.begin(), hop_, output_.begin());
        std::copy(overlap_.begin() + hop_, overlap_.end(), overlap_.begin());
        std::fill(overlap_.begin() + hop_, overlap_.end(), complex_t{0, 0});
        ++frames_;
    }

    int size_ = 0, hop_ = 0, fill_ = 0, read_ = 0;
    uint64_t frames_ = 0;
    float reduction_ = 12, smoothing_ = 0, noiseRise_ = 0, noiseFall_ = 0, gainRelease_ = 0;
    std::unique_ptr<fft::ComplexFFT> forward_, inverse_;
    std::vector<complex_t> input_, spectrum_, overlap_, output_;
    std::vector<float> window_, power_, smooth_, noise_, gains_, target_, previous_, floor_, tileScratch_;
};
}
