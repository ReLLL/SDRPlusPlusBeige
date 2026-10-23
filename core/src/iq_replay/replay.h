#pragma once
#include "store.h"
#include <dsp/stream.h>

namespace beige::iqreplay {
struct State {
    bool enabled = false, replaying = false;
    StoreState storage;
    std::string receiver, error;
    Stamp cursor, origin;
    double frequency = 0, desiredSeconds = 0;
    uint64_t revision = 0;
    uint64_t rendered = 0, underruns = 0, queuedAudio = 0;
};
using RadioUpdate = std::function<void()>;
using RadioTune = std::function<bool(double)>;
void registerRadio(const std::string& name, RadioUpdate update, RadioUpdate stop, RadioTune tune);
void removeRadio(const std::string& name);
bool enabled();
void enable(bool value);
State state();
void tick();
void shutdown();
void layout(int rows);
void invalidate();
void retune(double center);
void notice(const std::string& text);
void fault(const std::string& text);
std::string takeNotice();
bool seek(const std::string& receiver, const Stamp& at, double frequency);
bool tune(double frequency);
bool shiftTime(double seconds);
void live();
size_t read(const std::string& receiver, uint64_t revision, dsp::complex_t* output, size_t count);
void audio(const std::string& receiver, uint64_t revision, const dsp::stereo_t* data, size_t count, int rate);
bool renderAudio(void* listeningKey, dsp::stereo_t* output, size_t count, bool audioLive);
bool listeningActive(void* key);
void startFramer(dsp::stream<dsp::complex_t>* input, double rate, int keep, int interval,
    const std::function<void(dsp::complex_t*, int)>& fft);
void stopFramer();
Stamp frameStamp();
}
