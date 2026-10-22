#pragma once
#include "logmmse.h"

namespace dsp {

// Preserve the established LOGMMSE signal, level and overlap behavior. Keep this
// numerical seam independent of streams so it can be compared with 1.3.14.
class ClassicNR {
public:
    void configure(int rate, bool preferApple) {
        rate_ = rate;
        params_.forceWideband = true;
        params_.fftBackend = preferApple ? 1 : 0;
        reset();
    }

    void reset() {
        pending_ = arrays::npzeros_c(0);
        params_.reset();
    }

    // Algorithm changes retain learned noise, overlap and queued samples.
    void setVoice(bool enabled) { voice_ = enabled; }

    void setAcceleration(bool preferApple) {
        params_.fftBackend = preferApple ? 1 : 0;
        if (!params_.Xk_prev) return;
        const int n = params_.nFFT;
        const bool supported = preferApple && n > 1 && (n & (n - 1)) == 0;
        if (usesApple() == supported) return;
        // Only replace the transforms. Preserve overlap, pending samples and
        // learned noise so a math setting cannot restart the noise capture.
        auto forward = arrays::allocateFFTPlan(false, n, preferApple);
        auto reverse = arrays::allocateFFTPlan(true, n, preferApple);
        params_.forwardPlan = std::move(forward);
        params_.reversePlan = std::move(reverse);
    }

    int process(const complex_t* input, int count, complex_t* output, bool hold = false, int capacity = 1000000) {
        params_.hold = hold;
        pending_->insert(pending_->end(), input, input + count);
        const int frame = rate_ / 100;
        const int demand = params_.Xk_prev ? frame * 2 : frame * 28;
        if (pending_->size() < size_t(demand)) return 0;
        if (!params_.Xk_prev) logmmse::LogMMSE::logmmse_sample(pending_, rate_, 0.15f, &params_, 12);
        // A stream output buffer has a fixed capacity. Retained overlap can
        // otherwise make an almost-full input block produce too many samples.
        const int maxFrames = capacity / params_.len2;
        if (maxFrames == 0) return 0;
        const size_t maxSamples = size_t(maxFrames + 2) * params_.len2;
        auto work = pending_;
        if (work->size() > maxSamples) work = std::make_shared<std::vector<complex_t>>(pending_->begin(), pending_->begin() + maxSamples);
        auto result = logmmse::LogMMSE::logmmse_all(work, rate_, 0.15f, &params_, voice_, voice_);
        for (size_t i = 0; i < result->size(); ++i) output[i] = (*result)[i] * 4.0f;
        pending_->erase(pending_->begin(), pending_->begin() + result->size());
        return int(result->size());
    }

    bool usesApple() const { return arrays::fftPlanUsesApple(params_.forwardPlan); }

private:
    int rate_ = 192000;
    bool voice_ = false;
    arrays::ComplexArray pending_;
    logmmse::LogMMSE::SavedParamsC params_;
};
}
