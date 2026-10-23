#pragma once
#include "demod.h"
#include "radio_interface.h"
#include <iq_replay/replay.h>
#include <dsp/channel/rx_vfo.h>
#include <dsp/chain.h>
#include <dsp/filter/deephasis.h>
#include <dsp/filter/fir.h>
#include <dsp/multirate/rational_resampler.h>
#include <dsp/sink/handler_sink.h>
#include <atomic>
#include <thread>

// A separate listening-only decoder. The ordinary Radio VFO, AF splitter,
// Recorder taps and plugin stream notifications continue consuming live RF.
class IQReplayRadio {
public:
    explicit IQReplayRadio(std::string name) : name(std::move(name)) {}
    ~IQReplayRadio() { stop(); }
    void stop() { pipeline.reset(); signature.clear(); }
    bool tune(double frequency) { return pipeline && pipeline->tune(frequency); }
    void update(int mode, double bandwidth, double audioRate, bool highPass, double tau, const json& settings) {
        const auto s = beige::iqreplay::state();
        if (!s.replaying || s.receiver != name) { stop(); return; }
        const auto next = json::array({mode, bandwidth, audioRate, highPass, tau, settings,
            s.revision, s.cursor.rate, s.cursor.center}).dump();
        if (next == signature) return;
        stop();
        try {
            pipeline = std::make_unique<Pipeline>(name, s, mode, bandwidth, audioRate, highPass, tau, settings);
            signature = next;
        } catch (const std::exception& error) {
            beige::iqreplay::live();
            beige::iqreplay::notice(std::string("Could not start Radio IQ replay: ") + error.what());
        }
    }
private:
    struct Pipeline {
        std::string name;
        uint64_t revision;
        int outputRate = 0;
        ConfigManager config;
        dsp::stream<dsp::complex_t> unused, channel;
        std::unique_ptr<dsp::channel::RxVFO> vfo;
        std::unique_ptr<demod::Demodulator> demod;
        dsp::chain<dsp::stereo_t> chain;
        dsp::multirate::RationalResampler<dsp::stereo_t> resampler;
        dsp::filter::FIR<dsp::stereo_t, float> highpass;
        dsp::filter::Deemphasis<dsp::stereo_t> deemphasis;
        dsp::tap<float> taps{};
        dsp::sink::Handler<dsp::stereo_t> sink;
        std::atomic<bool> stopping{false};
        std::atomic<double> requestedOffset{0};
        double capturedCenter = 0, capturedRate = 0, filterBandwidth = 0;
        int reference = 0;
        std::thread feeder;
        bool started = false;
        bool tune(double frequency) {
            double offset = frequency - capturedCenter;
            if (reference == ImGui::WaterfallVFO::REF_LOWER) offset += filterBandwidth / 2;
            if (reference == ImGui::WaterfallVFO::REF_UPPER) offset -= filterBandwidth / 2;
            if (!std::isfinite(offset) || std::abs(offset) + filterBandwidth / 2 > capturedRate / 2) return false;
            requestedOffset = offset;
            return true;
        }

        static std::unique_ptr<demod::Demodulator> makeDemod(int mode) {
            switch (mode) {
            case RADIO_IFACE_MODE_NFM: return std::make_unique<demod::NFM>();
            case RADIO_IFACE_MODE_WFM: return std::make_unique<demod::WFM>();
            case RADIO_IFACE_MODE_AM: return std::make_unique<demod::AM>();
            case RADIO_IFACE_MODE_DSB: return std::make_unique<demod::DSB>();
            case RADIO_IFACE_MODE_USB: return std::make_unique<demod::USB>();
            case RADIO_IFACE_MODE_CW: return std::make_unique<demod::CW>();
            case RADIO_IFACE_MODE_LSB: return std::make_unique<demod::LSB>();
            case RADIO_IFACE_MODE_RAW: return std::make_unique<demod::RAW>();
            default: throw std::runtime_error("This modulation has no built-in Radio replay decoder.");
            }
        }
        Pipeline(const std::string& receiver, const beige::iqreplay::State& s, int mode,
            double bandwidth, double audioRate, bool highPass, double tau, const json& settings)
            : name(receiver), revision(s.revision) {
            if (!std::isfinite(audioRate) || audioRate < 8000 || audioRate > 192000 || !s.cursor ||
                !std::isfinite(bandwidth) || bandwidth <= 0 || bandwidth > s.cursor.rate)
                throw std::invalid_argument("Unsupported replay rate or bandwidth.");
            outputRate = int(audioRate);
            config.conf[name] = settings;
            config.conf[name]["WFM"]["rds"] = false;
            demod = makeDemod(mode);
            channel.setBufferSize(65536);
            demod->init(name, &config, &channel, bandwidth, audioRate);
            demod->AFSampRateChanged(audioRate);
            if (mode == RADIO_IFACE_MODE_WFM) static_cast<demod::WFM*>(demod.get())->listeningOnly();
            const double ifRate = demod->getIFSampleRate();
            capturedCenter = s.cursor.center; capturedRate = s.cursor.rate;
            filterBandwidth = bandwidth; reference = demod->getVFOReference();
            if (!tune(s.frequency))
                throw std::invalid_argument("The Radio filter extends outside the recorded spectrum. Narrow its bandwidth or select another point.");
            const double initialOffset = requestedOffset;
            vfo = std::make_unique<dsp::channel::RxVFO>(&unused, s.cursor.rate, ifRate, bandwidth, initialOffset);
            chain.init(demod->getOutput());
            resampler.init(nullptr, demod->getAFSampleRate(), audioRate);
            chain.addBlock(&resampler, demod->getPostProcEnabled());
            taps = dsp::taps::highPass(300, 100, audioRate);
            highpass.init(nullptr, taps);
            chain.addBlock(&highpass, highPass && demod->getHighPassAllowed());
            deemphasis.init(nullptr, tau > 0 ? tau : 50e-6, audioRate);
            chain.addBlock(&deemphasis, tau > 0 && demod->getDeempAllowed());
            sink.init(chain.out, [](dsp::stereo_t* data, int count, void* ctx) {
                auto* self = static_cast<Pipeline*>(ctx);
                beige::iqreplay::audio(self->name, self->revision, data, size_t(count), self->outputRate);
            }, this);
            try {
            started = true;
            sink.start(); chain.start(); demod->start();
            feeder = std::thread([this, rate = s.cursor.rate, initialOffset] {
                try {
                    std::vector<dsp::complex_t> input(size_t(std::clamp(rate / 100, 1.0, 32768.0)));
                    auto next = std::chrono::steady_clock::now();
                    double offset = initialOffset;
                    while (!stopping) {
                        const size_t count = beige::iqreplay::read(name, revision, input.data(), input.size());
                        if (!count) { std::this_thread::sleep_for(std::chrono::milliseconds(2)); continue; }
                        const double requested = requestedOffset;
                        if (requested != offset) { vfo->setOffset(requested); offset = requested; }
                        const int produced = vfo->process(int(count), input.data(), channel.writeBuf);
                        if (produced && !channel.swap(produced)) break;
                        next += std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(count / rate));
                        if (next < std::chrono::steady_clock::now() - std::chrono::milliseconds(100))
                            next = std::chrono::steady_clock::now();
                        std::this_thread::sleep_until(next);
                    }
                } catch (const std::exception& error) { beige::iqreplay::fault(std::string("Radio replay stopped: ") + error.what()); }
            });
            } catch (...) { shutdown(); throw; }
        }
        void shutdown() {
            stopping = true; channel.stopWriter();
            if (feeder.joinable()) feeder.join();
            if (started) { demod->stop(); chain.stop(); sink.stop(); started = false; }
        }
        ~Pipeline() {
            shutdown();
            demod.reset(); vfo.reset();
            if (taps.taps) dsp::taps::free(taps);
        }
    };
    std::string name, signature;
    std::unique_ptr<Pipeline> pipeline;
};
