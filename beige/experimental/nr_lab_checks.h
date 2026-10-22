#pragma once
#include <cassert>
#include <random>

namespace beige::research {
inline std::vector<complex_t> labRun(const std::vector<complex_t>& input, int rate, int chunk,
        const std::string& mode, bool apple) {
    dsp::ClassicNR original;
    original.configure(rate, apple);
    LogMMSEVariant candidate(rate, apple, mode);
    std::vector<complex_t> block(1000000), output;
    for (size_t at = 0; at < input.size();) {
        const int count = int((std::min)(size_t(chunk), input.size() - at));
        const int written = mode == "reference" ? original.process(input.data() + at, count, block.data())
            : candidate.process(input.data() + at, count, block.data());
        assert(written >= 0 && written <= int(block.size()));
        for (int i = 0; i < written; ++i) assert(std::isfinite(block[i].re) && std::isfinite(block[i].im));
        output.insert(output.end(), block.begin(), block.begin() + written);
        at += count;
    }
    return output;
}

inline float difference(const std::vector<complex_t>& a, const std::vector<complex_t>& b) {
    assert(a.size() == b.size());
    float maximum = 0;
    for (size_t i = 0; i < a.size(); ++i) maximum = (std::max)(maximum,
        (std::max)(std::abs(a[i].re - b[i].re), std::abs(a[i].im - b[i].im)));
    return maximum;
}

inline int labChecks() {
    for (int rate : {48000, 204800, 800000}) {
        std::mt19937 rng(65537);
        std::normal_distribution<float> noise(0, 0.02f);
        std::vector<complex_t> input(rate * 3);
        for (size_t i = 0; i < input.size(); ++i) input[i] = {
            noise(rng) + float(0.03 * std::cos(6.283185307179586 * 1700 * i / rate)),
            noise(rng) + float(0.03 * std::sin(6.283185307179586 * 1700 * i / rate))};
        for (bool apple : {false, true}) {
            const auto reference = labRun(input, rate, rate / 200, "reference", apple);
            assert(difference(reference, labRun(input, rate, rate / 200, "clone", apple)) == 0);
            const auto invariant = labRun(input, rate, rate / 200, "framewise", apple);
            for (int chunk : {317, rate / 50}) {
                assert(difference(invariant, labRun(input, rate, chunk, "framewise", apple)) < 1e-6f);
            }
            for (const auto& mode : {"cap", "fast", "smooth", "bias", "bootstrap", "tracker", "gh",
                    "wiener", "sqrtwiener", "balanced", "framewiener"}) {
                const auto output = labRun(input, rate, rate / 200, mode, apple);
                assert(output.size() == reference.size());
            }
        }
        std::fill(input.begin(), input.end(), complex_t{0, 0});
        for (const auto& mode : {"reference", "gh", "wiener", "framewise", "framewiener", "balanced"}) {
            labRun(input, rate, 317, mode, true);
        }
        std::cout << "Lab parity, partitions, modes and silence passed at " << rate << " S/s\n";
    }
    // Exercise the fixed output buffer at the largest supported research rate.
    std::vector<complex_t> large(1680000, complex_t{0.01f, 0.01f});
    labRun(large, 2400000, 600000, "framewiener", false);
    return 0;
}
}
