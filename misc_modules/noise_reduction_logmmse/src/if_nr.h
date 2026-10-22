#pragma once
#include <dsp/processor.h>
#include <atomic>
#include <chrono>
#include "classic_nr.h"

namespace dsp {

class IFNRLogMMSE : public Processor<complex_t, complex_t> {
    using base_type = Processor<complex_t, complex_t>;
public:
    ~IFNRLogMMSE() override { if (_block_init) stop(); }

    void setInput(stream<complex_t>* in) override {
        if (!_block_init) init(in);
        else base_type::setInput(in);
    }

    void setHold(bool value) { hold_.store(value); }
    void setDisableCpuDeactivation(bool value) { disableCpuDeactivation_.store(value); }
    void setAcceleration(bool enabled) { preferApple_.store(enabled); }
    void setVoice(bool enabled) { voice_.store(enabled); }
    void reset() { shouldReset_.store(true); }
    bool usesApple() const { return usesApple_.load(); }
    bool stoppedForCpu() const { return cpuStop_.load(); }

    int process(const complex_t* in, int count, complex_t* out, int sampleRate) {
        const bool apple = preferApple_.load();
        if (sampleRate != rate_) {
            engine_.configure(sampleRate, apple);
            rate_ = sampleRate;
            configuredApple_ = apple;
            usesApple_.store(engine_.usesApple());
            shouldReset_.store(false);
        }
        if (apple != configuredApple_) {
            engine_.setAcceleration(apple);
            configuredApple_ = apple;
        }
        if (shouldReset_.exchange(false)) engine_.reset();
        engine_.setVoice(voice_.load());
        const int written = engine_.process(in, count, out, hold_.load());
        usesApple_.store(engine_.usesApple());
        return written;
    }

    int run() override {
        const int count = _in->read();
        if (count < 0) return -1;
        const int rate = (std::max)(1, int(sigpath::iqFrontEnd.getSampleRate()));
        const auto begin = std::chrono::steady_clock::now();
        const int written = process(_in->readBuf, count, out.writeBuf, rate);
        cpuSeconds_ += std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
        audioSeconds_ += double(count) / rate;
        _in->flush();
        if (written > 0 && !out.swap(written)) return -1;
        if (audioSeconds_ >= 0.4) {
            const int usage = int(100 * cpuSeconds_ / audioSeconds_);
            percentUsage.store(usage);
            if (previousUsage_ >= 95 && usage >= 95 && !disableCpuDeactivation_.load()) cpuStop_.store(true);
            previousUsage_ = usage;
            cpuSeconds_ = audioSeconds_ = 0;
        }
        return count;
    }

    void start() override {
        if (!txBound_) {
            txHandler_.ctx = this;
            txHandler_.handler = [](bool active, void* ctx) { static_cast<IFNRLogMMSE*>(ctx)->setHold(active); };
            sigpath::txState.bindHandler(&txHandler_);
            txBound_ = true;
        }
        base_type::start();
    }

    void stop() override {
        base_type::stop();
        if (txBound_) {
            sigpath::txState.unbindHandler(&txHandler_);
            txBound_ = false;
        }
        percentUsage.store(-1);
    }

    std::atomic<int> percentUsage{-1};
    bool bypass = true; // UI-thread preprocessor enable state.

protected:
    void doStart() override {
        // Reset before launching the worker, including temporary chain restarts.
        shouldReset_.store(true);
        cpuStop_.store(false);
        previousUsage_ = 0;
        cpuSeconds_ = audioSeconds_ = 0;
        base_type::doStart();
    }

private:
    ClassicNR engine_;
    std::atomic<bool> shouldReset_{true}, hold_{false}, preferApple_{true}, usesApple_{false};
    std::atomic<bool> voice_{false};
    std::atomic<bool> disableCpuDeactivation_{false}, cpuStop_{false};
    int rate_ = 0, previousUsage_ = 0;
    double cpuSeconds_ = 0, audioSeconds_ = 0;
    bool configuredApple_ = true, txBound_ = false;
    EventHandler<bool> txHandler_;
};
}
