#include <volk/volk.h>
#include "classic_nr.h"
#include "baseband_mode.h"
#include <signal_path/signal_path.h>
#include "if_nr.h"
#include "../experimental/logmmse_variants.h"
#include <cassert>
#include <iostream>
#include <random>

using dsp::complex_t;
using namespace dsp::baseband;

void policyChecks() {
    for (int mode = RADIO_IFACE_MODE_NFM; mode <= RADIO_IFACE_MODE_RAW; ++mode) {
        const auto automatic = select(Profile::Automatic, mode, {mode});
        if (ssb(mode)) assert(automatic == Algorithm::Voice);
        else if (protectedMode(mode)) assert(automatic == Algorithm::Bypass);
        else assert(automatic == Algorithm::Original);
        assert(select(Profile::Original, mode, {mode}) == Algorithm::Original);
        assert(select(Profile::Digital, mode, {mode}) == Algorithm::Bypass);
    }
    assert(select(Profile::Automatic, -1, {}) == Algorithm::Original);
    assert(select(Profile::Automatic, -1, {RADIO_IFACE_MODE_USB}) == Algorithm::Original);
    assert(select(Profile::Automatic, RADIO_IFACE_MODE_USB, {RADIO_IFACE_MODE_LSB}) == Algorithm::Voice);
    for (int mode : std::vector<int>{RADIO_IFACE_MODE_AM, RADIO_IFACE_MODE_CW, RADIO_IFACE_MODE_DSB, -1}) {
        assert(select(Profile::Automatic, RADIO_IFACE_MODE_USB, {mode}) == Algorithm::Original);
    }
    assert(select(Profile::Automatic, RADIO_IFACE_MODE_USB, {RADIO_IFACE_MODE_RAW}) == Algorithm::Bypass);
    assert(select(Profile::Automatic, RADIO_IFACE_MODE_CW, {RADIO_IFACE_MODE_WFM}) == Algorithm::Bypass);
}

std::vector<complex_t> fixture(int rate) {
    std::mt19937 rng(65537);
    std::normal_distribution<float> noise(0, 0.02f);
    std::vector<complex_t> input(rate * 2);
    for (size_t i = 0; i < input.size(); ++i) {
        const double t = double(i) / rate;
        const float envelope = 0.03f * (1 + 0.8 * std::sin(6.283185307179586 * 4 * t));
        input[i] = {noise(rng) + envelope * float(std::cos(6.283185307179586 * 1700 * t)),
            noise(rng) + envelope * float(std::sin(6.283185307179586 * 1700 * t))};
    }
    return input;
}

std::vector<complex_t> voice(const std::vector<complex_t>& input, int rate, int chunk, bool apple, bool oracle) {
    dsp::ClassicNR nr;
    nr.configure(rate, apple);
    nr.setVoice(true);
    beige::research::LogMMSEVariant expected(rate, apple, "framewiener");
    std::vector<complex_t> output, block(1000000);
    for (size_t at = 0; at < input.size();) {
        const int count = int((std::min)(size_t(chunk), input.size() - at));
        const int written = oracle ? expected.process(input.data() + at, count, block.data())
            : nr.process(input.data() + at, count, block.data());
        assert(written >= 0 && written <= int(block.size()));
        for (int i = 0; i < written; ++i) assert(std::isfinite(block[i].re) && std::isfinite(block[i].im));
        output.insert(output.end(), block.begin(), block.begin() + written);
        at += count;
    }
    return output;
}

double error(const std::vector<complex_t>& a, const std::vector<complex_t>& b) {
    assert(a.size() == b.size());
    double sum = 0, power = 0;
    for (size_t i = 0; i < a.size(); ++i) {
        sum += std::pow(a[i].re - b[i].re, 2) + std::pow(a[i].im - b[i].im, 2);
        power += std::pow(a[i].re, 2) + std::pow(a[i].im, 2);
    }
    return std::sqrt(sum / (std::max)(power, 1e-20));
}

void numericalChecks() {
    for (int rate : {204800, 800000}) {
        auto input = fixture(rate);
        const auto portable = voice(input, rate, rate / 200, false, false);
        for (bool apple : {false, true}) {
            const auto actual = voice(input, rate, rate / 200, apple, false);
            assert(error(actual, voice(input, rate, rate / 200, apple, true)) < 1e-7);
            assert(error(actual, voice(input, rate, 317, apple, false)) < 1e-7);
            assert(error(actual, portable) < 1e-4);
        }
        std::fill(input.begin(), input.end(), complex_t{0, 0});
        voice(input, rate, 317, true, false);
        std::cout << "Voice oracle, packet independence, backends and silence passed at " << rate << '\n';
    }
}

void transitionChecks() {
    for (int rate : {204800, 800000}) {
        const auto input = fixture(rate);
        dsp::IFNRLogMMSE actual, reference;
        std::vector<complex_t> a(1000000), b(1000000);
        for (size_t at = 0; at < input.size();) {
            const int count = int((std::min)(size_t(rate / 200), input.size() - at));
            actual.setVoice((at / count) % 12 < 6);
            actual.setAcceleration((at / count) % 8 < 4);
            actual.setHold(at > size_t(rate) && at < size_t(rate * 1.3));
            const int n = actual.process(input.data() + at, count, a.data(), rate);
            const int expected = reference.process(input.data() + at, count, b.data(), rate);
            assert(n == expected); // No new capture or lost samples on method/math changes.
            for (int i = 0; i < n; ++i) {
                assert(std::isfinite(a[i].re) && std::isfinite(a[i].im));
                assert(std::abs(a[i].re) < 2 && std::abs(a[i].im) < 2);
            }
            at += count;
        }
        actual.reset();
        assert(actual.process(input.data(), rate / 200, a.data(), rate) == 0);
    }
    dsp::ClassicNR bounded;
    bounded.configure(800000, true);
    bounded.setVoice(true);
    const auto input = fixture(800000);
    std::vector<complex_t> output(8208, {12345, -54321});
    for (int i = 0; i < 5; ++i) {
        const int n = bounded.process(input.data(), 240000, output.data(), false, 8192);
        assert(n == 8000);
        for (int j = 8192; j < 8208; ++j) assert(output[j].re == 12345 && output[j].im == -54321);
    }
}

int run() {
    policyChecks(); numericalChecks(); transitionChecks();
    std::cout << "Mode-aware Baseband NR checks passed\n";
    return 0;
}
int main() {
    const int code = run();
    std::cout.flush();
    std::_Exit(code);
}
