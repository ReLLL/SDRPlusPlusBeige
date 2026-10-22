#pragma once
#include <algorithm>
#include <cmath>
#include <vector>

namespace beige {
// FFT boundaries are exclusive; the signal peak loop requires inclusive bin indices.
inline bool spectrumSignalInfo(const float* fft, int bins, double span, double center,
        double bandwidth, float& strength, float& snr) {
    if (!fft || bins < 2 || !std::isfinite(span) || span <= 0 ||
            !std::isfinite(center) || !std::isfinite(bandwidth) || bandwidth <= 0) return false;
    const double low = center - bandwidth / 2, high = center + bandwidth / 2;
    if (high < -span / 2 || low > span / 2) return false;
    auto boundary = [&](double frequency) {
        const double index = (frequency / (span / 2)) * (bins / 2) + (bins / 2);
        if (index <= 0) return 0;
        if (index >= bins) return bins;
        return int(index);
    };
    const int left = boundary(center - bandwidth), right = boundary(center + bandwidth);
    const int first = std::min(boundary(low), bins - 1);
    const int last = std::min(boundary(high), bins - 1);
    // Preserve the existing estimator, with independent reusable scratch per caller thread.
    thread_local std::vector<float> noise;
    noise.clear();
    double average = 0, quiet = 0;
    for (int i = left; i < first; ++i) { noise.push_back(fft[i]); average += fft[i]; }
    for (int i = last + 1; i < right; ++i) { noise.push_back(fft[i]); average += fft[i]; }
    if (noise.size() < 4) return false;
    std::sort(noise.begin(), noise.end());
    const auto quarter = noise.size() / 4;
    const float threshold = noise[quarter];
    for (float value : noise) if (value <= threshold) quiet += value;
    quiet /= double(quarter);
    average /= double(noise.size());
    float peak = -INFINITY;
    for (int i = first; i <= last; ++i) peak = std::max(peak, fft[i]);
    const float level = peak - (average - quiet), ratio = level - average;
    if (!std::isfinite(level) || !std::isfinite(ratio)) return false;
    strength = level;
    snr = ratio;
    return true;
}
}
