#include <volk/volk.h>
#include "classic_nr.h"
#include <signal_path/signal_path.h>
#include "if_nr.h"
#include <cassert>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <random>
#include <vector>

using dsp::complex_t;
using Clock = std::chrono::steady_clock;

std::vector<complex_t> fixture(int rate, int kind) {
    std::vector<complex_t> data(rate);
    std::mt19937 random(104729);
    std::normal_distribution<float> noise(0, 0.01f);
    for (int i = 0; i < rate; ++i) {
        const double t = double(i) / rate;
        const double angle = 6.283185307179586 * 1700 * t;
        const float envelope = 0.1f * (0.6f + 0.4f * std::sin(6.283185307179586 * 4 * t));
        if (kind == 0 || (kind == 2 && t < 0.4)) continue;
        data[i] = {float(envelope * std::cos(angle)) + noise(random),
            float(envelope * std::sin(angle)) + noise(random)};
    }
    return data;
}

std::vector<complex_t> reduce(const std::vector<complex_t>& input, int rate, bool apple, bool fragmented) {
    dsp::ClassicNR nr;
    nr.configure(rate, apple);
    std::vector<complex_t> output, block(1000000);
    for (size_t at = 0; at < input.size();) {
        const int count = int((std::min)(input.size() - at,
            size_t(fragmented ? 317 : rate / 50)));
        const int written = nr.process(input.data() + at, count, block.data());
        for (int i = 0; i < written; ++i) {
            assert(std::isfinite(block[i].re) && std::isfinite(block[i].im));
        }
        output.insert(output.end(), block.begin(), block.begin() + written);
        at += count;
    }
    return output;
}

void numericalChecks() {
    for (float value : {0.0f, -1.0f, 1e-30f, 1.0f, 1e30f,
            std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()}) {
        assert(std::isfinite(dsp::math::expn(value)));
    }
    for (int rate : {204800, 819200, 800000}) {
        for (int kind : {0, 1, 2}) {
            const auto input = fixture(rate, kind);
            for (bool fragmented : {false, true}) {
                const auto reference = reduce(input, rate, false, fragmented);
                const auto actual = reduce(input, rate, true, fragmented);
                assert(actual.size() == reference.size());
                const size_t count = (std::min)(actual.size(), reference.size());
                assert(count > size_t(rate / 2));
                double error = 0, power = 0;
                for (size_t i = 0; i < count; ++i) {
                    const double re = actual[i].re - reference[i].re;
                    const double im = actual[i].im - reference[i].im;
                    error += re * re + im * im;
                    power += double(reference[i].re) * reference[i].re + double(reference[i].im) * reference[i].im;
                }
                const double relative = std::sqrt(error / (std::max)(power, 1e-20));
                std::cout << "NR equivalence rate=" << rate << " fixture=" << kind
                    << " fragmented=" << fragmented << " relative_rms=" << relative << '\n';
                assert(relative < 0.0001);
            }
        }
    }
}

void backendSwitchChecks() {
    for (int rate : {204800, 800000}) {
        const auto input = fixture(rate, 1);
        const auto reference = reduce(input, rate, false, false);
        dsp::IFNRLogMMSE nr;
        nr.setAcceleration(false);
        std::vector<complex_t> actual, block(1000000);
        for (size_t at = 0; at < input.size();) {
            const int count = int((std::min)(input.size() - at, size_t(rate / 50)));
            // Change backends repeatedly after the initial noise capture.
            nr.setAcceleration(at > size_t(rate / 2) && ((at / count) % 8 < 4));
            const int written = nr.process(input.data() + at, count, block.data(), rate);
            actual.insert(actual.end(), block.begin(), block.begin() + written);
            at += count;
        }
        assert(actual.size() == reference.size());
        double error = 0, power = 0;
        for (size_t i = 0; i < actual.size(); ++i) {
            const double re = actual[i].re - reference[i].re, im = actual[i].im - reference[i].im;
            error += re * re + im * im;
            power += double(reference[i].re) * reference[i].re + double(reference[i].im) * reference[i].im;
        }
        assert(std::sqrt(error / power) < 0.0001);
        std::cout << "Continuous NR backend switches passed at " << rate << " S/s\n";
    }
}

void benchmark() {
    using namespace dsp::arrays;
    for (int n : {8192, 32000, 32768, 65536, 262144}) {
        auto input = npzeros_c(n);
        std::mt19937 random(104729);
        std::normal_distribution<float> noise(0, 0.01f);
        for (auto& value : *input) value = {noise(random), noise(random)};
        for (bool apple : {false, true}) {
            auto plan = allocateFFTPlan(false, n, apple);
            std::vector<double> times;
            for (int repeat = 0; repeat < 7; ++repeat) {
                const auto start = Clock::now();
                for (int i = 0; i < 100; ++i) plan->npfftfft(input);
                times.push_back(std::chrono::duration<double, std::micro>(Clock::now() - start).count() / 100);
            }
            std::sort(times.begin(), times.end());
            std::cout << "FFT n=" << n << " requested=" << apple
                << " backend=" << (fftPlanUsesApple(plan) ? "Apple" : "FFTW")
                << " median_us=" << times[3] << '\n';
        }
    }
    for (int rate : {204800, 800000, 819200}) {
        const auto input = fixture(rate, 1);
        for (bool apple : {false, true}) {
            const auto start = Clock::now();
            for (int i = 0; i < 3; ++i) reduce(input, rate, apple, false);
            std::cout << "NR rate=" << rate << " requested=" << apple << " cpu_percent="
                << std::chrono::duration<double>(Clock::now() - start).count() / 3 * 100 << '\n';
        }
    }
}

int run(int argc) {
    if (argc > 1) benchmark();
    else { backendSwitchChecks(); numericalChecks(); }
    return 0;
}

int main(int argc, char**) {
    const int result = run(argc);
    std::cout.flush();
    // Core global services require GUI-owned shutdown; local objects are gone.
    std::_Exit(result);
}
