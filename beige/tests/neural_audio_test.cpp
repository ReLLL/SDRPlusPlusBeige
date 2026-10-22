#include "neural_audio.h"
#include "audio_resampler.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>
#include <utils/flog.h>

namespace flog {
std::string __toString__(const char* value) { return value; }
std::string __toString__(int value) { return std::to_string(value); }
std::string formatString(const char* message, const std::vector<std::string>&) { return message; }
void __log__(Type, const char* message, const std::vector<std::string>&) { std::cerr << message << '\n'; }
}

static std::vector<dsp::stereo_t> run(const std::string& model,
    const std::vector<dsp::stereo_t>& input, int rate, int chunk, bool apple, unsigned options = 0) {
    dsp::NeuralVoiceEngine engine(model, rate, apple);
    engine.setClarityOptions(options);
    std::vector<dsp::stereo_t> output(input.size() + 16, {9, 11});
    for (int offset = 0; offset < int(input.size()); offset += chunk) {
        const int count = (std::min)(chunk, int(input.size()) - offset);
        engine.process(input.data() + offset, count, output.data() + offset);
    }
    for (int i = input.size(); i < int(output.size()); ++i) assert(output[i].l == 9 && output[i].r == 11);
    output.resize(input.size());
    for (auto value : output) assert(std::isfinite(value.l) && value.l == value.r);
    return output;
}

static void compare(const std::vector<dsp::stereo_t>& a, const std::vector<dsp::stereo_t>& b, double tolerance) {
    double error = 0, power = 0;
    assert(a.size() == b.size());
    for (size_t i = 0; i < a.size(); ++i) { error += std::pow(a[i].l - b[i].l, 2); power += a[i].l * a[i].l; }
    const double relative = std::sqrt(error / (std::max)(power, 1e-20));
    std::cout << "Relative RMS: " << relative << '\n';
    assert(relative < tolerance);
}

int main(int argc, char** argv) {
    assert(argc == 2);
    const std::string model = argv[1];
    {
        std::vector<dsp::stereo_t> input(24000, {.15f, .15f});
        for (unsigned options = 0; options < 8; ++options) {
            auto a = run(model, input, 48000, 1984, true, options);
            compare(a, run(model, input, 48000, 317, true, options), 1e-7);
            if (options == 3) compare(a, run(model, input, 48000, 1024, false, options), 1e-4);
        }
        dsp::NeuralVoiceEngine engine(model, 48000, true);
        std::vector<dsp::stereo_t> output(input.size());
        for (unsigned options : {1, 2, 4, 7, 0}) {
            engine.setClarityOptions(options);
            engine.process(input.data(), input.size(), output.data());
            for (auto v : output) assert(std::isfinite(v.l) && v.l == v.r);
        }
    }
    {
        std::vector<dsp::stereo_t> input(48000);
        uint32_t seed = 71;
        for (int i = 0; i < 48000; ++i) {
            seed = 1664525 * seed + 1013904223;
            const float noise = (float(seed >> 8) / 16777216.f - .5f) * .2f;
            const float voice = .25f * std::sin(2 * 3.141592653589793 * 720 * i / 48000);
            input[i] = {voice + noise, voice + noise};
        }
        auto normal = run(model, input, 48000, 1984, true);
        for (auto& value : input) { value.l *= 4; value.r *= 4; }
        auto loud = run(model, input, 48000, 1984, true);
        for (auto& value : loud) { value.l /= 4; value.r /= 4; }
        normal.erase(normal.begin(), normal.begin() + 24000);
        loud.erase(loud.begin(), loud.begin() + 24000);
        compare(normal, loud, .02);
    }
    for (int rate : {8000, 16000, 44100, 48000, 96000, 192000}) {
        std::vector<dsp::stereo_t> input(rate);
        for (int i = 0; i < rate; ++i) {
            const float signal = i < rate / 4 ? 0 : .15f * std::sin(2 * 3.141592653589793 * 720 * i / rate);
            input[i] = {signal, signal};
        }
        auto a = run(model, input, rate, 1984, true);
        auto b = run(model, input, rate, 317, true);
        compare(a, b, 1e-7);
        if (rate == 48000) compare(a, run(model, input, rate, 1024, false), 1e-4);
    }
    for (int frequency : {1000, 11000}) {
        dsp::AudioResampler<float> resampler;
        resampler.configure(48000, 16000);
        double power = 0; int samples = 0, total = 0;
        for (int i = 0; i < 48000; ++i) resampler.push(float(std::sin(2 * 3.141592653589793 * frequency * i / 48000)), [&](float value) {
            ++total;
            if (i > 4800) { power += value * value; ++samples; }
        });
        assert(total == 16000);
        const double rms = std::sqrt(power / samples);
        if (frequency == 1000) assert(rms > .69 && rms < .72);
        else assert(rms < .01);
    }
    dsp::NeuralVoiceNR processor;
    std::vector<dsp::stereo_t> input(1000, {.12f, -.06f}), output(1000);
    assert(processor.process(input.data(), 1000, output.data()) == 1000);
    assert(output[10].l == .12f && output[10].r == -.06f);
    processor.allowed = true;
    processor.voiceMode = false;
    processor.process(input.data(), 1000, output.data());
    assert(output[10].l == .12f && output[10].r == -.06f);
    processor.voiceMode = true;
    processor.modelPath = "/nonexistent/beige-test-model.onnx";
    processor.process(input.data(), 1000, output.data());
    assert(processor.status == -1 && output[10].l == .12f && output[10].r == -.06f);
    dsp::AudioCpuMeter meter;
    const double before = dsp::AudioCpuMeter::now();
    meter.add(before, 48000, 48000);
    assert(meter.percent >= 0 && std::isfinite(meter.percent.load()));
    meter.reset(); assert(meter.percent == -1);
    std::cout << "Neural NR: packet independence, FFT parity, six rates, bounded output, alias rejection, bypass, missing-model fallback passed.\n";
}
