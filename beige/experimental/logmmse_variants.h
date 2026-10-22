#pragma once

// Research only. No production module includes this file. Keep transform,
// window, overlap and level identical while changing one estimator at a time.
#include "logmmse.h"
#include <string>

namespace beige::research {
using namespace dsp;
using namespace dsp::arrays;
using Params = logmmse::LogMMSE::SavedParamsC;

class LogMMSEVariant {
public:
    LogMMSEVariant(int rate, bool apple, std::string mode)
        : rate_(rate), mode_(std::move(mode)), pending_(npzeros_c(0)) {
        params_.forceWideband = true;
        params_.fftBackend = apple ? 1 : 0;
    }

    int process(const complex_t* input, int count, complex_t* output) {
        pending_->insert(pending_->end(), input, input + count);
        if (pending_->size() < size_t((params_.Xk_prev ? 2 : 28) * (rate_ / 100))) return 0;
        if (!params_.Xk_prev) {
            logmmse::LogMMSE::logmmse_sample(pending_, rate_, 0.15f, &params_, 12);
            if (mode_ == "fast") params_.aa = 0.94f;
            if (mode_ == "balanced") params_.aa = 0.96f;
            if (mode_ == "bootstrap" || mode_ == "tracker" || mode_ == "gh" || mode_ == "balanced") bootstrap();
        }
        auto work = pending_;
        const size_t maximum = size_t(1000000 / params_.len2 + 2) * params_.len2;
        if (work->size() > maximum) work = std::make_shared<std::vector<complex_t>>(pending_->begin(), pending_->begin() + maximum);
        const auto result = frames(work);
        for (size_t i = 0; i < result->size(); ++i) output[i] = (*result)[i] * 4.0f;
        pending_->erase(pending_->begin(), pending_->begin() + result->size());
        return int(result->size());
    }

private:
    void bootstrap() {
        const int n = params_.nFFT, step = 128, width = 256;
        const int tiles = (n + step - 1) / step;
        std::vector<float> levels(tiles), scratch(width);
        // A lower quartile of averaged magnitudes rejects occupied subbands.
        // Calibration is fixed, not derived from the test's clean reference.
        for (int tile = 0; tile < tiles; ++tile) {
            for (int j = 0; j < width; ++j) scratch[j] = (*params_.sums)[(tile * step + j - width / 2 + n) % n] / 12;
            std::nth_element(scratch.begin(), scratch.begin() + width / 4, scratch.end());
            levels[tile] = (std::max)(1e-20f, scratch[width / 4] * scratch[width / 4] * 1.6f);
        }
        for (int i = 0; i < n; ++i) {
            const int tile = i / step;
            const float mix = float(i % step) / step;
            (*params_.noise_mu2)[i] = levels[tile] * (1 - mix) + levels[(tile + 1) % tiles] * mix;
        }
    }

    ComplexArray frames(const ComplexArray& data) {
        const int n = params_.nFFT;
        const int frames = int(data->size() / params_.len2) - params_.Slen / params_.len2;
        const bool tracked = mode_ == "tracker" || mode_ == "gh";
        const bool frameClock = mode_ == "framewise" || mode_ == "framewiener";
        if (!tracked && !frameClock) params_.update_noise_mu2(data);
        npmaximum_(params_.noise_mu2, 1e-20f);
        auto output = npzeros_c(frames * params_.len2);
        for (int k = 0; k < frames * params_.len2; k += params_.len2) {
            // Advance the noise estimator with FFT frames, not USB packets.
            if (frameClock) params_.update_noise_mu2(data);
            auto input = muleach(params_.win, nparange(data, k, k + params_.Slen));
            npfftfft(input, params_.forwardPlan);
            auto spectrum = params_.forwardPlan->getOutput();
            auto magnitude = npabsolute(spectrum);
            for (int i = 1; i < n; ++i) {
                if ((*magnitude)[i] == 0) (*magnitude)[i] = (*magnitude)[i - 1];
            }
            if (!tracked) params_.add_noise_history(magnitude);
            auto power = muleach(magnitude, magnitude);
            if (mode_ == "gh") {
                if (probabilityMean_.empty()) probabilityMean_.assign(n, 0.5f);
                // Gerkmann/Hendriks (2012), equations 18, 22, 23, 24.
                // Fixed 15 dB H1 prior; do not couple noise estimation to DD gain.
                constexpr float priorH1 = 31.6227766f;
                for (int i = 0; i < n; ++i) {
                    const float old = (*params_.noise_mu2)[i];
                    const float gamma = (*power)[i] / old;
                    float p = 1.0f / (1.0f + (1 + priorH1) * std::exp(-gamma * priorH1 / (1 + priorH1)));
                    probabilityMean_[i] = 0.9f * probabilityMean_[i] + 0.1f * p;
                    if (probabilityMean_[i] > 0.99f) p = (std::min)(p, 0.99f);
                    const float estimate = p * old + (1 - p) * (*power)[i];
                    (*params_.noise_mu2)[i] = (std::max)(1e-20f, 0.8f * old + 0.2f * estimate);
                }
            }
            auto noise = mode_ == "bias" ? mul(params_.noise_mu2, 4.0f / FL_M_PI) : params_.noise_mu2;
            auto posterior = npminimum_(diveach(power, noise), 40);
            FloatArray prior;
            if (!npall(params_.Xk_prev)) {
                prior = add(mul(npmaximum_(add(posterior, -1), 0), 1 - params_.aa), params_.aa);
            } else {
                auto a = diveach(mul(params_.Xk_prev, params_.aa), noise);
                auto b = mul(npmaximum_(add(posterior, -1), 0), 1 - params_.aa);
                prior = npmaximum_(addeach(a, b), params_.ksi_min);
            }
            auto wiener = diveach(prior, add(prior, 1));
            auto v = muleach(wiener, posterior);
            FloatArray gain;
            if (mode_ == "wiener" || mode_ == "sqrtwiener" || mode_ == "framewiener") {
                gain = wiener;
                if (mode_ == "sqrtwiener") for (auto& value : *gain) value = std::sqrt(value);
            } else {
                gain = muleach(wiener, npexp(mul(scipyspecialexpn(v), 0.5f)));
            }
            if (mode_ == "cap" || mode_ == "smooth") gain = npminimum_(gain, 1);
            if (mode_ == "smooth") {
                if (previousGain_.empty()) previousGain_.assign(n, 1);
                auto original = *gain;
                for (int i = 0; i < n; ++i) {
                    // Widen open regions rather than attenuating a CW peak.
                    float g = (std::max)(original[i],
                        (original[(i + n - 1) % n] + 2 * original[i] + original[(i + 1) % n]) * 0.25f);
                    g = (std::max)(g, 0.08f);
                    if (g < previousGain_[i]) g = 0.78f * previousGain_[i] + 0.22f * g;
                    (*gain)[i] = previousGain_[i] = g;
                }
            }
            if (mode_ == "tracker") {
                for (int i = 0; i < n; ++i) {
                    // Soft signal-presence-controlled recursion inspired by
                    // Cohen's LSA model; this is not a full IMCRA implementation.
                    const float presence = 1.0f / (1.0f + 4.0f * (1 + (*prior)[i]) * std::exp(-(*v)[i]));
                    const float alpha = 0.98f + 0.02f * presence;
                    (*params_.noise_mu2)[i] = (std::max)(1e-20f,
                        alpha * (*params_.noise_mu2)[i] + (1 - alpha) * (*power)[i]);
                }
            }
            magnitude = muleach(magnitude, gain);
            params_.Xk_prev = muleach(magnitude, magnitude);
            npfftfft(muleach(gain, spectrum), params_.reversePlan);
            auto inverse = params_.reversePlan->getOutput();
            nparangeset(output, k, addeach(params_.x_old, nparange(inverse, 0, params_.len1)));
            params_.x_old = nparange(inverse, params_.len1, params_.Slen);
        }
        return output;
    }

    int rate_;
    std::string mode_;
    ComplexArray pending_;
    Params params_;
    std::vector<float> previousGain_;
    std::vector<float> probabilityMean_;
};
}
