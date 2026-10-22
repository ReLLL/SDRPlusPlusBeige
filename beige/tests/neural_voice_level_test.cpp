#include "neural_voice_level.h"
#include <array>
#include <cassert>
#include <iostream>

int main() {
    dsp::NeuralVoiceLevel level;
    std::array<dsp::complex_t,257> dry{}, wet{};
    dry[20] = {10, 3}; wet[20] = {5, 1.5};
    // Rejected noise must not set the recovery gain.
    for (int i = 40; i < 100; ++i) { dry[i] = {10, 0}; wet[i] = {.001f, 0}; }
    for (int i = 0; i < 200; ++i) level.update(dry.data(), wet.data());
    assert(std::abs(level.gain() - 2.f) < .01f);
    dry.fill({0, 0}); wet.fill({0, 0});
    for (int i = 0; i < 200; ++i) level.update(dry.data(), wet.data());
    assert(std::abs(level.gain() - 2.f) < .01f);
    for (int i = 4; i <= 128; ++i) { dry[i] = {10, 0}; wet[i] = {.001f, 0}; }
    for (int i = 0; i < 200; ++i) level.update(dry.data(), wet.data());
    assert(std::abs(level.gain() - 2.f) < .01f);
    dry.fill({0, 0}); wet.fill({0, 0});
    dry[20] = wet[20] = {10, 3};
    for (int i = 0; i < 200; ++i) level.update(dry.data(), wet.data());
    assert(std::abs(level.gain() - 1.f) < .01f);
    dry[20] = {100, 30}; wet[20] = {11, 3.3};
    for (int i = 0; i < 200; ++i) level.update(dry.data(), wet.data());
    assert(level.gain() <= 4 && level.gain() > 3.99f);
    level.reset(); assert(level.gain() == 1);
    std::cout << "Voice level: recovery, rejected noise, silence hold, clean unity and gain bound passed.\n";
}
