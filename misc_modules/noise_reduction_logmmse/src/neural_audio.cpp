#ifdef BEIGE_NEURAL_AUDIO
#include "neural_audio.h"
#include "audio_resampler.h"
#include "neural_voice_level.h"
#include "neural_clarity.h"
#include <onnxruntime_cxx_api.h>
#include <utils/complex_fft.h>
#include <array>
#include <cmath>
#include <stdexcept>
#include <utils/flog.h>

namespace dsp {
struct NeuralVoiceEngine::State {
    static Ort::Env& environment() {
        static Ort::Env env(ORT_LOGGING_LEVEL_ERROR, "Beige neural voice");
        return env;
    }
    static Ort::SessionOptions options() {
        Ort::SessionOptions result;
        result.SetIntraOpNumThreads(1);
        result.SetInterOpNumThreads(1);
        result.AddConfigEntry("session.intra_op.allow_spinning", "0");
        result.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        return result;
    }

    State(const std::string& path, int sampleRate, bool apple)
        : session(environment(), path.c_str(), options()),
          forward(512, false, apple), inverse(512, true, apple), rate(sampleRate) {
        down.configure(rate, 16000);
        up.configure(16000, rate);
        gainSmoothing = 1.f - std::exp(-1.f / (.02f * rate));
        for (int i = 0; i < 512; ++i)
            window[i] = std::sqrt(.5 - .5 * std::cos(2 * pi * i / 512));
        auto memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        const std::vector<std::vector<int64_t>> shapes{{1,257,1,2}, {2,1,16,16,33}, {2,3,1,1,16}, {2,1,33,16}};
        for (int i = 0; i < 4; ++i) {
            size_t size = 1;
            for (auto value : shapes[i]) size *= value;
            input[i].resize(size);
            output[i].resize(size);
            inputs.push_back(Ort::Value::CreateTensor<float>(memory, input[i].data(), size, shapes[i].data(), shapes[i].size()));
            outputs.push_back(Ort::Value::CreateTensor<float>(memory, output[i].data(), size, shapes[i].data(), shapes[i].size()));
        }
        reset();
    }

    void reset() {
        down.reset(); up.reset();
        for (auto& value : input) std::fill(value.begin(), value.end(), 0);
        history.fill(0); overlap.fill(0);
        frameCount = readIndex = writeIndex = queued = 0;
        ramp = 0;
        modelLevel = 0;
        voiceLevel.reset();
        clarity.reset();
        detailMix = 0;
        liftGain = 1;
        outputGain = 1;
        // Keep enough queued samples to cover a 16 ms inference hop.
        const int padding = (rate + 49) / 50 + 8;
        for (int i = 0; i < padding; ++i) enqueue({0, 0});
    }

    void enqueue(stereo_t value) {
        if (queued == queue.size()) throw std::runtime_error("Neural audio queue overflow");
        queue[writeIndex] = value;
        writeIndex = (writeIndex + 1) % queue.size();
        ++queued;
    }

    void frame() {
        std::copy(history.begin() + 256, history.end(), history.begin());
        std::copy(pending.begin(), pending.end(), history.begin() + 256);
        double power = 0;
        for (float value : history) power += double(value) * value;
        modelLevel = (std::max)(float(std::sqrt(power / 512)), .98f * modelLevel);
        // Radio AGC levels can exceed training audio. Attenuate for inference,
        // then undo that gain before overlap-add; do not raise quiet input/noise.
        const float modelGain = modelLevel > .1f ? .1f / modelLevel : 1.f;
        for (int i = 0; i < 512; ++i) bins[i] = {history[i] * window[i], 0};
        forward.execute(bins.data());
        for (int i = 0; i < 257; ++i) {
            input[0][2*i] = bins[i].re * modelGain;
            input[0][2*i+1] = bins[i].im * modelGain;
        }
        static const char* names[] = {"mix", "conv_cache", "tra_cache", "inter_cache"};
        static const char* resultNames[] = {"enh", "conv_cache_out", "tra_cache_out", "inter_cache_out"};
        session.Run(Ort::RunOptions{nullptr}, names, inputs.data(), 4, resultNames, outputs.data(), 4);
        for (int i = 1; i < 4; ++i) std::copy(output[i].begin(), output[i].end(), input[i].begin());
        for (int i = 0; i < 257; ++i) enhanced[i] = {output[0][2*i] / modelGain, output[0][2*i+1] / modelGain};
        clarity.frame(bins.data(), enhanced.data(), clarityOptions);
        voiceLevel.update(bins.data(), enhanced.data());
        std::copy(enhanced.begin(), enhanced.end(), bins.begin());
        for (int i = 1; i < 256; ++i) bins[512-i] = {bins[i].re, -bins[i].im};
        inverse.execute(bins.data());
        for (int i = 0; i < 256; ++i) {
            const float wet = bins[i].re * window[i] + overlap[i];
            overlap[i] = bins[i+256].re * window[i+256];
            if (!std::isfinite(wet)) throw std::runtime_error("Nonfinite neural output");
            up.push(stereo_t{history[i], wet}, [this](stereo_t value) { enqueue(value); });
        }
    }

    void process(const stereo_t* source, int count, stereo_t* destination) {
        // Work sample by sample so FIFO bounds are independent of caller packet size.
        for (int i = 0; i < count; ++i) {
            const float mono = .5f * (source[i].l + source[i].r);
            down.push(std::isfinite(mono) ? mono : 0, [this](float sample) {
                pending[frameCount++] = sample;
                if (frameCount == 256) { frame(); frameCount = 0; }
            });
            if (!queued) throw std::runtime_error("Neural audio queue underrun");
            const auto value = queue[readIndex];
            readIndex = (readIndex + 1) % queue.size();
            --queued;
            ramp = (std::min)(1.f, ramp + 1.f / (.1f * rate));
            outputGain += gainSmoothing * (voiceLevel.gain() - outputGain);
            // Aligned dry/wet paths; ease into the independent voice reducer.
            float mixed;
            if (clarityOptions || detailMix > 1e-6f || liftGain != 1.f) {
                float wet = value.r * outputGain;
                const float detail = clarityOptions & NeuralClarity::Detail ? .15f * clarity.speech() : 0;
                detailMix += gainSmoothing * (detail - detailMix);
                if (!detail && detailMix < 1e-6f) detailMix = 0;
                const float lift = clarityOptions & NeuralClarity::Lift ? 1.f + .12201845f * clarity.speech() : 1.f;
                liftGain += gainSmoothing * (lift - liftGain);
                if (lift == 1.f && std::abs(liftGain - 1.f) < 1e-5f) liftGain = 1;
                wet = (wet + detailMix * (value.l - wet)) * liftGain;
                mixed = value.l + ramp * (wet - value.l);
            }
            else mixed = value.l + ramp * (value.r * outputGain - value.l);
            destination[i] = {mixed, mixed};
        }
    }

    static constexpr double pi = 3.14159265358979323846;
    Ort::Session session;
    fft::ComplexFFT forward, inverse;
    AudioResampler<float> down;
    AudioResampler<stereo_t> up;
    std::array<std::vector<float>,4> input, output;
    std::vector<Ort::Value> inputs, outputs;
    std::array<float,512> window{}, history{};
    std::array<complex_t,512> bins{};
    std::array<complex_t,257> enhanced{};
    std::array<float,256> pending{}, overlap{};
    std::array<stereo_t,8192> queue{};
    int rate, frameCount = 0;
    size_t readIndex = 0, writeIndex = 0, queued = 0;
    float ramp = 0;
    float modelLevel = 0, outputGain = 1, gainSmoothing = 0;
    NeuralVoiceLevel voiceLevel;
    NeuralClarity clarity;
    unsigned clarityOptions = 0;
    float detailMix = 0, liftGain = 1;
};

NeuralVoiceEngine::NeuralVoiceEngine(const std::string& model, int rate, bool apple)
    : state_(std::make_unique<State>(model, rate, apple)) {}
NeuralVoiceEngine::~NeuralVoiceEngine() = default;
void NeuralVoiceEngine::reset() { state_->reset(); }
void NeuralVoiceEngine::setClarityOptions(unsigned options) { state_->clarityOptions = options & NeuralClarity::All; }
void NeuralVoiceEngine::process(const stereo_t* input, int count, stereo_t* output) { state_->process(input, count, output); }
bool NeuralVoiceEngine::usesApple() const { return state_->forward.usesApple(); }

int NeuralVoiceNR::process(const stereo_t* input, int count, stereo_t* output) {
    if (!allowed.load() || !voiceMode.load()) {
        std::copy(input, input + count, output);
        active_ = false;
        cpuMeter.reset();
        return count;
    }
    if (status.load() < 0) { std::copy(input, input + count, output); return count; }
    try {
        if (!engine_ || configuredRate_ != sampleRate.load()) {
            const int rate = sampleRate.load();
            engine_ = std::make_unique<NeuralVoiceEngine>(modelPath, rate, true);
            configuredRate_ = rate;
            appleFFT.store(engine_->usesApple());
            active_ = true;
        }
        if (!active_) engine_->reset();
        active_ = true;
        const auto start = AudioCpuMeter::now();
        engine_->setClarityOptions(clarityOptions.load());
        engine_->process(input, count, output);
        cpuMeter.add(start, count, configuredRate_);
        status.store(1);
    } catch (const std::exception& error) {
        flog::warn("Neural voice NR unavailable; bypassing: {}", error.what());
        status.store(-1);
        cpuMeter.reset();
        std::copy(input, input + count, output);
    }
    return count;
}

int NeuralVoiceNR::run() {
    const int count = _in->read();
    if (count < 0) return -1;
    const int written = process(_in->readBuf, count, out.writeBuf);
    _in->flush();
    if (written > 0 && !out.swap(written)) return -1;
    return count;
}

void NeuralVoiceNR::stop() {
    Processor<stereo_t,stereo_t>::stop();
    active_ = false;
    cpuMeter.reset();
}
}
#endif
