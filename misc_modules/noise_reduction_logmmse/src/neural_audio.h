#pragma once
#include <atomic>
#include <memory>
#include <string>
#include <dsp/processor.h>
#include "audio_cpu_meter.h"

namespace dsp {
class NeuralVoiceEngine {
public:
    NeuralVoiceEngine(const std::string& model, int rate, bool apple);
    ~NeuralVoiceEngine();
    void reset();
    void setClarityOptions(unsigned options);
    void process(const stereo_t* input, int count, stereo_t* output);
    bool usesApple() const;
private:
    struct State;
    std::unique_ptr<State> state_;
};

class NeuralVoiceNR : public Processor<stereo_t, stereo_t> {
public:
    std::atomic<bool> allowed{false}, voiceMode{true};
    std::atomic<int> sampleRate{48000}, status{0}; // 0 idle, 1 ready, -1 failed
    std::atomic<bool> appleFFT{false};
    std::atomic<unsigned> clarityOptions{0};
    AudioCpuMeter cpuMeter;
    std::string modelPath;

    ~NeuralVoiceNR() override { if (_block_init) stop(); }
    int process(const stereo_t* input, int count, stereo_t* output);
    int run() override;
    void stop() override;
private:
    std::unique_ptr<NeuralVoiceEngine> engine_;
    bool active_ = false;
    int configuredRate_ = 0;
};
}
