#include "../experimental/adaptive_nr.h"
#include "bgnoise.h"
#ifndef BEIGE_NR_STANDALONE
#include <volk/volk.h>
#include "logmmse.h"
#include "classic_nr.h"
#endif
#include <cassert>
#include <chrono>
#include <complex>
#include <iostream>
#include <random>
#include <string>
#include <limits>

using dsp::complex_t;
constexpr double testPi = 3.14159265358979323846;

std::vector<complex_t> makeInput(int rate, double seconds, bool tone = true) {
    std::mt19937 random(20261022);
    std::normal_distribution<float> noise(0, 0.03f);
    std::vector<complex_t> data(int(rate * seconds));
    for (size_t i = 0; i < data.size(); ++i) {
        const double angle = 2 * testPi * 1700 * i / rate;
        data[i] = {noise(random) + float(tone ? 0.025 * std::cos(angle) : 0),
            noise(random) + float(tone ? 0.025 * std::sin(angle) : 0)};
    }
    return data;
}

std::vector<complex_t> modern(const std::vector<complex_t>& input, int rate, bool apple, float db = 12) {
    dsp::AdaptiveNR nr;
    nr.configure(rate, apple);
    nr.setReduction(db);
    std::vector<complex_t> output(input.size());
    nr.process(input.data(), int(input.size()), output.data());
    return output;
}

#ifndef BEIGE_NR_STANDALONE
std::vector<complex_t> legacy(const std::vector<complex_t>& input, int rate) {
    using namespace dsp::arrays;
    using dsp::logmmse::LogMMSE;
    enableAcceleratedFFT = false;
    LogMMSE::SavedParamsC params;
    params.forceWideband = true;
    auto pending = npzeros_c(0);
    std::vector<complex_t> result;
    const int chunk = rate / 50;
    for (int at = 0; at + chunk <= int(input.size()); at += chunk) {
        pending->insert(pending->end(), input.begin() + at, input.begin() + at + chunk);
        if (!params.Xk_prev && pending->size() < size_t(28 * (rate / 100))) continue;
        if (!params.Xk_prev) LogMMSE::logmmse_sample(pending, rate, 0.15f, &params, 12);
        auto out = LogMMSE::logmmse_all(pending, rate, 0.15f, &params);
        result.insert(result.end(), out->begin(), out->end());
        pending->erase(pending->begin(), pending->begin() + out->size());
    }
    return result;
}
#endif

void classicRegressionChecks() {
#ifndef BEIGE_NR_STANDALONE
    for (int rate : {192000, 204800}) {
        const auto input = makeInput(rate, 3);
        const auto reference = legacy(input, rate);
        for (bool apple : {false, true}) {
            dsp::ClassicNR nr;
            nr.configure(rate, apple);
            std::vector<complex_t> result, block(1000000);
            const int chunk = rate / 50;
            for (int at = 0; at + chunk <= int(input.size()); at += chunk) {
                const int written = nr.process(input.data() + at, chunk, block.data());
                result.insert(result.end(), block.begin(), block.begin() + written);
            }
            assert(result.size() == reference.size());
            float error = 0;
            for (size_t i = 0; i < result.size(); ++i) {
                error = (std::max)(error, std::abs(result[i].re - reference[i].re * 4));
                error = (std::max)(error, std::abs(result[i].im - reference[i].im * 4));
            }
            std::cout << "Original NR equivalence " << rate << " Hz, " << (apple ? "auto" : "FFTW") << ": max error " << error << '\n';
            assert(error < (apple ? 0.001f : 1e-7f));
        }
    }
    dsp::ClassicNR nr;
    nr.configure(192000, false);
    auto block = makeInput(192000, 0.5);
    std::vector<complex_t> bounded(4096);
    for (int i = 0; i < 5; ++i) {
        const int written = nr.process(block.data(), int(block.size()), bounded.data(), false, int(bounded.size()));
        assert(written >= 0 && written <= int(bounded.size()));
    }
#endif
}

void fftChecks() {
    for (int n : {256, 1024, 7680, 8192}) {
        auto original = makeInput(48000, double(n) / 48000);
        original.resize(n);
        auto a = original, b = original;
        dsp::fft::ComplexFFT fa(n, false, true), ff(n, false, false);
        if ((n & (n - 1)) != 0) assert(!fa.usesApple());
        fa.execute(a.data()); ff.execute(b.data());
        for (int i = 0; i < n; ++i) {
            assert(std::abs(a[i].re - b[i].re) < 0.001f);
            assert(std::abs(a[i].im - b[i].im) < 0.001f);
        }
        dsp::fft::ComplexFFT inverse(n, true, true);
        inverse.execute(a.data());
        for (int i = 0; i < n; ++i) {
            assert(std::abs(a[i].re - original[i].re) < 1e-5f);
            assert(std::abs(a[i].im - original[i].im) < 1e-5f);
        }
    }
}

void streamChecks() {
    for (int rate : {48000, 192000, 256000, 768000, 2400000}) {
        auto input = makeInput(rate, 0.25);
        dsp::AdaptiveNR nr;
        nr.configure(rate, true);
        nr.setReduction(0);
        std::vector<complex_t> output(input.size());
        for (int at = 0; at < int(input.size());) {
            const int count = (std::min)(137, int(input.size()) - at);
            nr.process(input.data() + at, count, output.data() + at);
            at += count;
        }
        for (int i = nr.latency(); i < int(output.size()); ++i) {
            assert(std::abs(output[i].re - input[i - nr.latency()].re) < 1e-5f);
            assert(std::abs(output[i].im - input[i - nr.latency()].im) < 1e-5f);
        }
        nr.setReduction(12);
        nr.reset();
        auto partitioned = output;
        for (int at = 0; at < int(input.size());) {
            const int count = (std::min)(317, int(input.size()) - at);
            nr.process(input.data() + at, count, partitioned.data() + at);
            at += count;
        }
        auto whole = modern(input, rate, true);
        auto portable = modern(input, rate, false);
        for (size_t i = 0; i < whole.size(); ++i) {
            assert(std::isfinite(whole[i].re) && std::isfinite(whole[i].im));
            assert(std::abs(whole[i].re - partitioned[i].re) < 1e-6f);
            assert(std::abs(whole[i].im - partitioned[i].im) < 1e-6f);
            assert(std::abs(whole[i].re - portable[i].re) < 2e-5f);
            assert(std::abs(whole[i].im - portable[i].im) < 2e-5f);
        }
    }
}

void qualityChecks() {
    const int rate = 192000;
    auto input = makeInput(rate, 4);
    auto output = modern(input, rate, true);
    dsp::AdaptiveNR nr; nr.configure(rate, true);
    const int latency = nr.latency();
    double inNoise = 0, outNoise = 0;
    std::complex<double> inTone{0}, outTone{0};
    int count = 0;
    for (int i = rate; i + latency < int(output.size()); ++i) {
        const auto reference = std::polar(1.0, -2 * testPi * 1700 * i / rate);
        std::complex<double> a(input[i].re, input[i].im), b(output[i + latency].re, output[i + latency].im);
        inTone += a * reference; outTone += b * reference;
        const auto wanted = std::conj(reference) * 0.025;
        inNoise += std::norm(a - wanted); outNoise += std::norm(b - wanted);
        ++count;
    }
    const double improvement = 10 * std::log10(inNoise / outNoise);
    const double toneDb = 20 * std::log10(std::abs(outTone / inTone));
    std::cout << "Quality: " << improvement << " dB residual-noise reduction; " << toneDb << " dB wanted-tone level change\n";
    assert(improvement > 5);
    assert(std::abs(toneDb) < 0.75);
#ifndef BEIGE_NR_STANDALONE
    const auto old = legacy(input, rate);
    double oldError = 0, oldInError = 0;
    std::complex<double> oldTone{0}, oldInputTone{0};
    for (int i = rate; i < int(old.size()); ++i) {
        const auto reference = std::polar(1.0, -2 * testPi * 1700 * i / rate);
        const auto wanted = std::conj(reference) * 0.025;
        const std::complex<double> before(input[i].re, input[i].im), after(old[i].re, old[i].im);
        oldError += std::norm(after - wanted);
        oldInError += std::norm(before - wanted);
        oldTone += after * reference; oldInputTone += before * reference;
    }
    std::cout << "Legacy, normalized before its 4x output boost: " << 10 * std::log10(oldInError / oldError)
        << " dB residual-noise reduction; " << 20 * std::log10(std::abs(oldTone / oldInputTone)) << " dB wanted-tone level change\n";
#endif
    std::fill(input.begin(), input.end(), complex_t{0, 0});
    output = modern(input, rate, true);
    for (const auto& v : output) assert(v.re == 0 && v.im == 0);
}

void modulationChecks() {
    const int rate = 192000;
    for (const std::string mode : {"SSB", "AM", "NFM", "BPSK", "CW"}) {
        auto input = makeInput(rate, 4, false);
        std::vector<complex_t> clean(input.size());
        for (size_t i = 0; i < input.size(); ++i) {
            const double t = double(i) / rate;
            std::complex<double> s;
            if (mode == "SSB") {
                for (int k = 0; k < 28; ++k) {
                    const double envelope = 0.6 + 0.4 * std::sin(2 * testPi * (3 + k % 5) * t + k);
                    s += std::polar(0.012 * envelope, 2 * testPi * (300 + k * 87) * t + k * k);
                }
            }
            if (mode == "AM") s = std::polar(0.1 * (1 + 0.6 * std::sin(2 * testPi * 700 * t)), 2 * testPi * 8000 * t);
            if (mode == "NFM") s = std::polar(0.1, -2 * testPi * 6000 * t + 2 * std::sin(2 * testPi * 700 * t));
            if (mode == "BPSK") s = (0.07 * std::tanh(8 * std::sin(2 * testPi * 15.625 * t))) * std::polar(1.0, 2 * testPi * 3700 * t);
            if (mode == "CW") s = std::polar(0.07 * (std::fmod(t, 0.3) < 0.2 ? 1 : 0), -2 * testPi * 1700 * t);
            clean[i] = {float(s.real()), float(s.imag())};
            input[i] += clean[i];
        }
        auto output = modern(input, rate, true);
        auto cleanOut = modern(clean, rate, true);
        dsp::AdaptiveNR nr; nr.configure(rate, true);
        const int delay = nr.latency();
        double inError = 0, outError = 0, cleanError = 0, energy = 0;
        for (int i = rate; i + delay < int(output.size()); ++i) {
            const std::complex<double> s(clean[i].re, clean[i].im);
            inError += std::norm(std::complex<double>(input[i].re, input[i].im) - s);
            outError += std::norm(std::complex<double>(output[i + delay].re, output[i + delay].im) - s);
            cleanError += std::norm(std::complex<double>(cleanOut[i + delay].re, cleanOut[i + delay].im) - s);
            energy += std::norm(s);
        }
        const double improvement = 10 * std::log10(inError / outError);
        const double cleanEvm = std::sqrt(cleanError / energy);
        std::cout << mode << ": " << improvement << " dB error reduction, " << 100 * cleanEvm << "% clean-signal EVM\n";
        assert(improvement > 3);
        assert(cleanEvm < 0.1);
    }
}

void changingNoiseChecks() {
    const int rate = 192000;
    auto input = makeInput(rate, 5, false);
    float re = 0, im = 0;
    for (size_t i = 0; i < input.size(); ++i) {
        re = 0.85f * re + input[i].re;
        im = 0.85f * im + input[i].im;
        const float scale = i < size_t(2 * rate) ? 1 : 4;
        input[i] = {re * scale, im * scale};
    }
    auto output = modern(input, rate, true);
    dsp::AdaptiveNR nr; nr.configure(rate, true);
    const int delay = nr.latency();
    for (int start : {rate, 4 * rate}) {
        double inEnergy = 0, outEnergy = 0;
        for (int i = start; i < start + rate / 2; ++i) {
            inEnergy += input[i].re * input[i].re + input[i].im * input[i].im;
            outEnergy += output[i + delay].re * output[i + delay].re + output[i + delay].im * output[i + delay].im;
        }
        const double reduction = 10 * std::log10(inEnergy / outEnergy);
        std::cout << "Colored noise at " << start / rate << " s: " << reduction << " dB reduction\n";
        assert(reduction > 6);
    }
}

void robustnessChecks() {
    BackgroundNoiseCaltulator noise;
    std::vector<float> values(256, 1);
    assert(std::isfinite(noise.addFrame(values)));
    noise.reset();
    for (int i = 0; i < 256; ++i) values[i] = float(i);
    assert(std::isfinite(noise.addFrame(values))); // maximum belongs to last bucket, not bucket N
    noise.reset();
    std::fill(values.begin(), values.end(), 0);
    assert(std::isfinite(noise.addFrame(values)));

    dsp::AdaptiveNR nr;
    nr.configure(192000, true);
    auto input = makeInput(192000, 0.1);
    auto output = input;
    input[9].re = std::numeric_limits<float>::quiet_NaN();
    input[29].im = std::numeric_limits<float>::infinity();
    nr.process(input.data(), int(input.size()), output.data(), true);
    for (const auto& value : output) assert(std::isfinite(value.re) && std::isfinite(value.im));
    // Reconfiguration and retune discard all retained audio/IQ.
    for (int i = 0; i < 20; ++i) {
        nr.configure(i % 2 ? 48000 : 768000, i % 2);
        nr.reset();
        std::fill(input.begin(), input.end(), complex_t{0, 0});
        nr.process(input.data(), int(input.size()), output.data());
        for (const auto& value : output) assert(value.re == 0 && value.im == 0);
    }
}

double channelError(const std::vector<complex_t>& received, const std::vector<complex_t>& wanted, int delay) {
    constexpr int n = 4096;
    dsp::fft::ComplexFFT fft(n, false, false);
    std::vector<complex_t> error(n);
    double energy = 0;
    for (int at = 192000; at + n + delay < int(received.size()); at += n) {
        for (int i = 0; i < n; ++i) {
            error[i] = complex_t{received[at + i + delay].re - wanted[at + i].re,
                received[at + i + delay].im - wanted[at + i].im} * float(std::sin(testPi * (i + 0.5) / n));
        }
        fft.execute(error.data());
        // 300-3000 Hz USB audio passband at 192 kS/s.
        for (int k = 7; k <= 64; ++k) energy += error[k].re * error[k].re + error[k].im * error[k].im;
    }
    return energy;
}

void weakChannelChecks() {
    const int rate = 192000;
    auto input = makeInput(rate, 4, false);
    std::vector<complex_t> clean(input.size());
    for (size_t i = 0; i < input.size(); ++i) {
        const double t = double(i) / rate;
        std::complex<double> s{0};
        for (int k = 0; k < 28; ++k) {
            const double envelope = (std::max)(0.0, std::sin(2 * testPi * (2 + k % 4) * t + k));
            s += std::polar(0.012 * envelope, 2 * testPi * (300 + k * 87) * t + k * k);
        }
        clean[i] = {float(s.real()), float(s.imag())};
        input[i] *= 10.0f; // approximately 0 dB channel SNR, not an easy wideband tone test
        input[i] += clean[i];
    }
    dsp::AdaptiveNR nr; nr.configure(rate, true);
    const double before = channelError(input, clean, 0);
    for (float db : {6.0f, 12.0f, 18.0f}) {
        auto output = modern(input, rate, true, db);
        const double improvement = 10 * std::log10(before / channelError(output, clean, nr.latency()));
        std::cout << "Weak SSB, 300-3000 Hz channel, " << db << " dB setting: " << improvement << " dB error reduction\n";
        assert(improvement > 1.0);
    }
}

int runTests(int argc, char** argv) {
    if (argc > 1) {
        const std::string mode = argv[1];
        const int rate = argc > 2 ? std::stoi(argv[2]) : 192000;
        const int repeats = argc > 3 ? std::stoi(argv[3]) : 1;
        auto input = makeInput(rate, 20);
        const auto start = std::chrono::steady_clock::now();
        for (int repeat = 0; repeat < repeats; ++repeat) {
#ifndef BEIGE_NR_STANDALONE
            if (mode == "legacy") legacy(input, rate);
            else
#endif
            {
                auto out = modern(input, rate, mode != "portable");
                assert(out.size() == input.size());
            }
        }
        const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        std::cout << mode << " " << rate << " samples/s: " << elapsed << " s for " << 20 * repeats << " s IQ, " << 5 * elapsed / repeats << "% of one realtime core\n";
        return 0;
    }
    classicRegressionChecks(); fftChecks(); streamChecks(); qualityChecks(); modulationChecks(); changingNoiseChecks(); robustnessChecks(); weakChannelChecks();
    std::cout << "Baseband NR FFT, reconstruction, stream partitions, backends and quality checks passed.\n";
    return 0;
}

int main(int argc, char** argv) {
    const int result = runTests(argc, argv);
    std::cout.flush();
#ifndef BEIGE_NR_STANDALONE
    // The GUI core's global services require application-owned shutdown.
    // All local test/denoiser state has been destroyed before this point.
    std::_Exit(result);
#else
    return result;
#endif
}
