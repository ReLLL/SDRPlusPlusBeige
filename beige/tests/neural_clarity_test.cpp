#include "neural_clarity.h"
#include <array>
#include <cassert>
#include <cstring>
#include <iostream>

int main() {
    dsp::NeuralClarity clarity;
    std::array<dsp::complex_t,257> dry{}, wet{};
    dry[20] = {1, 0}; wet[20] = {.8f, 0};
    const auto original = wet;
    clarity.frame(dry.data(), wet.data(), 0);
    assert(std::memcmp(wet.data(), original.data(), sizeof(wet)) == 0);
    for (int i = 0; i < 16; ++i) {
        wet = original;
        clarity.frame(dry.data(), wet.data(), dsp::NeuralClarity::Smooth);
    }
    assert(clarity.speech() > .99f);
    wet[20] = {.01f, 0};
    clarity.frame(dry.data(), wet.data(), dsp::NeuralClarity::Smooth);
    assert(wet[20].re > .1f && wet[20].re <= .36f);
    for (int i = 0; i < 100; ++i) {
        wet.fill({.001f, 0}); dry.fill({1, 0});
        clarity.frame(dry.data(), wet.data(), dsp::NeuralClarity::Detail);
    }
    assert(clarity.speech() < 1e-6f);
    for (unsigned mask = 0; mask <= dsp::NeuralClarity::All; ++mask) {
        clarity.reset();
        for (int i = 0; i < 100; ++i) {
            for (int k = 0; k < 257; ++k) wet[k] = {float(i % 3) * .1f, .01f};
            clarity.frame(dry.data(), wet.data(), mask);
            for (auto value : wet) assert(std::isfinite(value.re) && std::isfinite(value.im));
        }
    }
    clarity.frame(dry.data(), wet.data(), 0);
    std::cout << "Clarity: exact off path, bounded smoothing, speech hold/release and eight combinations passed.\n";
}
