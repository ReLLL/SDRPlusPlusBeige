#include "replay.h"
#include "framer.h"
#include "retention.h"
#include <audio/rewind.h>
#include <core.h>
#include <signal_path/signal_path.h>
#include <gui/gui.h>
#include <http_debug_server.h>
#include <atomic>
#include <chrono>
#include <cmath>
#include <map>

namespace beige::iqreplay {
namespace {
std::mutex control;
std::shared_ptr<Store> store;
struct Radio { RadioUpdate update, stop; RadioTune tune; };
std::map<std::string, Radio> radios;
State transport;
std::string pendingNotice;
std::atomic<void*> audibleKey{nullptr};
std::atomic<bool> feature{false};
std::atomic<bool> captureStopped{true};
bool initialized = false, reconfigure = false;
int visibleRows = 400;
std::chrono::steady_clock::time_point layoutChanged{};
std::deque<dsp::stereo_t> samples;
bool audioReady = false;
int audioRate = 48000;
std::unique_ptr<std::thread> frameWorker;
std::atomic<bool> stopFrames{false};
dsp::stream<dsp::complex_t>* frameInput = nullptr;
thread_local Stamp currentFrame;

int64_t nowNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}
void initialize() {
    if (initialized) return;
    initialized = true;
    feature = core::configManager.conf.value("waterfallReplay", true);
    reconfigure = feature;
    using namespace httpdebug::procfs;
    registerEndpoint("/iq-replay/enabled", [] { return enabled() ? "true" : "false"; },
        [](const std::string& value) { enable(value == "true"); }, Type::Bool);
    registerEndpoint("/iq-replay/status", [] {
        const auto s = state(); const auto& b = s.storage;
        return json{{"enabled", s.enabled}, {"ready", b.ready}, {"starting", b.starting},
            {"replaying", s.replaying}, {"receiver", s.receiver}, {"frequency", s.frequency},
            {"epoch", b.epoch}, {"cursor_epoch", s.cursor.epoch}, {"cursor", s.cursor.sample},
            {"selected_sample", s.origin.sample},
            {"audio_rendered", s.rendered}, {"audio_underruns", s.underruns}, {"queued_audio", s.queuedAudio},
            {"head", b.head}, {"captured", b.captured}, {"oldest", b.oldest()}, {"rate", b.rate},
            {"center", b.center}, {"capacity_samples", b.capacity}, {"bytes", b.capacity * sizeof(dsp::complex_t)},
            {"visible_rows", visibleRows}, {"desired_seconds", s.desiredSeconds}, {"revision", s.revision},
            {"queued_bytes", b.queuedBytes}, {"error", b.error}, {"notice", s.error}}.dump();
    }, nullptr, Type::String);
    registerEndpoint("/iq-replay/live", [] { return state().replaying ? "false" : "true"; },
        [](const std::string& value) { if (value == "true") { live(); rewind::current(); } }, Type::Bool);
    registerEndpoint("/iq-replay/seek", [] { return "receiver, epoch, sample, frequency"; },
        [](const std::string& value) {
            try {
                auto j = json::parse(value); auto b = state().storage;
                seek(j.at("receiver"), {j.at("epoch"), j.at("sample"), b.rate, b.center, b.utcNs}, j.at("frequency"));
            } catch (const std::exception&) { notice("Invalid IQ replay point."); }
        }, Type::String);
    registerEndpoint("/iq-replay/frequency", [] { return std::to_string(state().frequency); },
        [](const std::string& value) {
            try { tune(std::stod(value)); } catch (const std::exception&) { notice("Invalid replay frequency."); }
        }, Type::Float);
    registerEndpoint("/iq-replay/time-step", [] { return "Signed seconds from the current IQ playback time"; },
        [](const std::string& value) {
            try { shiftTime(std::stod(value)); } catch (const std::exception&) { notice("Invalid replay time step."); }
        }, Type::Float);
}
}

void registerRadio(const std::string& name, RadioUpdate update, RadioUpdate stop, RadioTune tune) {
    initialize(); radios[name] = {std::move(update), std::move(stop), std::move(tune)};
}
void removeRadio(const std::string& name) {
    if (state().receiver == name) live();
    radios.erase(name);
}
bool enabled() { return feature.load(std::memory_order_relaxed); }
void notice(const std::string& text) { std::lock_guard<std::mutex> lock(control); pendingNotice = text; }
void fault(const std::string& text) { std::lock_guard<std::mutex> lock(control); transport.error = text; }
std::string takeNotice() { std::lock_guard<std::mutex> lock(control); auto text = std::move(pendingNotice); pendingNotice.clear(); return text; }
State state() {
    State result;
    std::shared_ptr<Store> saved;
    { std::lock_guard<std::mutex> lock(control); result = transport; result.queuedAudio = samples.size(); saved = store; }
    result.enabled = enabled();
    if (saved) result.storage = saved->state(); else result.storage.starting = false;
    return result;
}
void live() {
    std::string old;
    {
        std::lock_guard<std::mutex> lock(control);
        old = transport.receiver; transport.replaying = false; transport.receiver.clear();
        ++transport.revision; samples.clear(); audioReady = false; audibleKey = nullptr;
    }
    auto it = radios.find(old); if (it != radios.end()) it->second.stop();
}
void enable(bool value) {
    initialize();
    if (enabled() == value) return;
    live(); feature = value; reconfigure = true;
    if (value) { std::lock_guard<std::mutex> lock(control); transport.error.clear(); }
    core::configManager.acquire(); core::configManager.conf["waterfallReplay"] = value; core::configManager.release(true);
}
void layout(int rows) {
    rows = std::clamp(rows, 1, 32768);
    if (rows == visibleRows) return;
    visibleRows = rows; layoutChanged = std::chrono::steady_clock::now();
}
void invalidate() {
    live();
    std::shared_ptr<Store> saved;
    { std::lock_guard<std::mutex> lock(control); saved = store; }
    if (saved) saved->reset(saved->state().center, nowNs());
}
void retune(double center) {
    auto s = state();
    if (!enabled() || !s.storage.rate || center == s.storage.center) return;
    live();
    std::shared_ptr<Store> saved;
    { std::lock_guard<std::mutex> lock(control); saved = store; }
    if (saved) saved->reset(center, nowNs());
}
void tick() {
    initialize();
    captureStopped = !gui::mainWindow.isPlaying();
    if (enabled()) {
        auto s = state();
        if (!s.storage.error.empty() || !s.error.empty()) {
            const auto error = s.storage.error.empty() ? s.error : s.storage.error;
            { std::lock_guard<std::mutex> lock(control); transport.error = error; }
            enable(false); notice(error + " Live reception continues.");
        }
    }
    if (reconfigure) {
        reconfigure = false;
        sigpath::iqFrontEnd.setReplayFraming(enabled());
        if (!enabled()) {
            std::shared_ptr<Store> previous;
            { std::lock_guard<std::mutex> lock(control); previous.swap(store); }
        }
    }
    if (enabled()) {
        const double rate = sigpath::iqFrontEnd.getEffectiveSamplerate();
        Retention retention{};
        try { retention = Retention::plan(rate, sigpath::iqFrontEnd.getFFTRate(), visibleRows); }
        catch (const std::exception& error) { fault(error.what()); return; }
        const auto s = state();
        const bool stable = std::chrono::steady_clock::now() - layoutChanged >= std::chrono::milliseconds(300);
        if (!s.storage.rate || s.storage.rate != rate || (stable && retention.replace(s.storage.capacity))) {
            live();
            try {
                // Release the old reservation before checking space for its replacement.
                std::shared_ptr<Store> previous;
                { std::lock_guard<std::mutex> lock(control); previous.swap(store); }
                previous.reset();
                const auto directory = std::filesystem::path(core::getRoot()) / "cache";
                auto next = std::make_shared<Store>(rate, gui::waterfall.getCenterFrequency(), nowNs(),
                    [directory](uint64_t bytes) { return temporaryBacking(directory, bytes); }, retention.seconds);
                { std::lock_guard<std::mutex> lock(control); store = std::move(next); transport.desiredSeconds = retention.seconds; }
            } catch (const std::exception& error) { fault(error.what()); enable(false); notice(error.what()); }
        }
        const auto active = state();
        if (active.replaying && (active.cursor.epoch != active.storage.epoch || active.cursor.sample < active.storage.oldest())) {
            live(); notice("That IQ history expired. Returned to live reception.");
        }
    }
    const auto active = state();
    auto it = radios.find(active.receiver);
    if (active.replaying && it != radios.end()) it->second.update();
    static std::chrono::steady_clock::time_point endedAt{};
    if (active.replaying && !gui::mainWindow.isPlaying() && active.cursor.sample >= active.storage.head && !active.queuedAudio) {
        const auto now = std::chrono::steady_clock::now();
        if (endedAt == std::chrono::steady_clock::time_point{}) endedAt = now;
        if (now - endedAt > std::chrono::milliseconds(100)) { live(); notice("Reached the end of IQ history. Returned to live."); }
    } else endedAt = {};
}
bool seek(const std::string& name, const Stamp& at, double frequency) {
    auto it = radios.find(name);
    if (it == radios.end()) { notice("Select a Radio receiver to replay waterfall IQ."); return false; }
    const auto stream = sigpath::sinkManager.streams.find(name);
    if (stream == sigpath::sinkManager.streams.end()) { notice("That Radio listening stream is unavailable."); return false; }
    const auto s = state(); const auto& b = s.storage;
    if (!s.enabled || !retainedPoint(b, at, frequency)) {
        notice("That waterfall point is not in the retained IQ history."); return false;
    }
    live();
    {
        std::lock_guard<std::mutex> lock(control);
        transport.replaying = true; transport.receiver = name; transport.cursor = at; transport.frequency = frequency;
        transport.origin = at;
        transport.rendered = transport.underruns = 0;
        ++transport.revision; audibleKey = stream->second;
    }
    rewind::current(name); it->second.update(); return true;
}
bool tune(double frequency) {
    const auto s = state();
    const auto it = radios.find(s.receiver);
    if (!s.replaying || it == radios.end()) return false;
    if (!retainedPoint(s.storage, s.cursor, frequency) || !it->second.tune(frequency)) {
        notice("That frequency or Radio filter is outside the retained IQ spectrum."); return false;
    }
    std::lock_guard<std::mutex> lock(control);
    if (!transport.replaying || transport.revision != s.revision) return false;
    transport.frequency = frequency;
    return true;
}
bool shiftTime(double seconds) {
    const auto s = state();
    if (!std::isfinite(seconds) || seconds == 0) return false;
    const auto& receiver = gui::waterfall.selectedVFO;
    const bool replay = s.replaying && s.receiver == receiver;
    if (!replay && seconds > 0) return false;
    if (!s.enabled) { notice("Enable Waterfall replay in Radio to seek back in time."); return false; }
    const auto vfo = gui::waterfall.vfos.find(receiver);
    if (vfo == gui::waterfall.vfos.end() || !vfo->second) return false;
    if (!s.storage.ready || !s.storage.rate || !s.storage.captured) {
        notice("IQ history is still filling. Try again shortly."); return false;
    }
    auto at = replay ? s.cursor : Stamp{s.storage.epoch, s.storage.head, s.storage.rate, s.storage.center, s.storage.utcNs};
    const double frequency = replay ? s.frequency : gui::waterfall.getCenterFrequency() + vfo->second->generalOffset;
    const long double sample = static_cast<long double>(at.sample) + std::round(static_cast<long double>(seconds) * at.rate);
    if (!std::isfinite(sample) || sample < s.storage.oldest()) {
        notice(replay ? "Reached the oldest retained IQ history." : "Not enough retained IQ history for that step.");
        return false;
    }
    if (sample >= s.storage.captured) {
        if (replay && seconds > 0) { rewind::live(); return true; }
        notice("That IQ history is still being saved. Try again shortly."); return false;
    }
    at.sample = static_cast<uint64_t>(sample);
    return seek(receiver, at, frequency);
}
size_t read(const std::string& name, uint64_t revision, dsp::complex_t* output, size_t count) {
    std::shared_ptr<Store> saved; Stamp at;
    {
        std::lock_guard<std::mutex> lock(control);
        if (!transport.replaying || transport.receiver != name || transport.revision != revision) return 0;
        saved = store; at = transport.cursor;
    }
    if (!saved) return 0;
    const size_t n = saved->read(at, output, count);
    if (n) {
        std::lock_guard<std::mutex> lock(control);
        if (transport.revision != revision) return 0;
        transport.cursor.sample += n;
    }
    return n;
}
bool listeningActive(void* key) { return key && audibleKey.load(std::memory_order_relaxed) == key; }
void audio(const std::string& name, uint64_t revision, const dsp::stereo_t* data, size_t count, int rate) {
    std::lock_guard<std::mutex> lock(control);
    if (!transport.replaying || transport.receiver != name || transport.revision != revision) return;
    audioRate = std::clamp(rate, 8000, 192000);
    const size_t limit = size_t(audioRate) * 2;
    if (count > limit) return;
    while (samples.size() + count > limit) samples.pop_front();
    samples.insert(samples.end(), data, data + count);
}
bool renderAudio(void* key, dsp::stereo_t* output, size_t count, bool audioLive) {
    if (!audioLive || !listeningActive(key)) return false;
    std::lock_guard<std::mutex> lock(control);
    if (!transport.replaying || audibleKey != key) return false;
    if (!audioReady && samples.size() < count + size_t(audioRate / 50)) {
        // A stopped capture's newest row may contain less than the preroll.
        // Play that short tail instead of waiting forever for more RF samples.
        if (!captureStopped || samples.empty() || !store || transport.cursor.sample < store->state().head) return false;
    }
    audioReady = true;
    for (size_t i = 0; i < count; ++i) {
        if (samples.empty()) ++transport.underruns;
        output[i] = samples.empty() ? dsp::stereo_t{} : samples.front();
        if (!samples.empty()) samples.pop_front();
    }
    transport.rendered += count;
    return true;
}
void startFramer(dsp::stream<dsp::complex_t>* input, double rate, int keep, int interval,
    const std::function<void(dsp::complex_t*, int)>& fft) {
    stopFramer(); frameInput = input; stopFrames = false;
    frameWorker = std::make_unique<std::thread>([=] {
        try {
        Framer framer{size_t(keep), size_t(interval)};
        while (!stopFrames) {
            const int count = input->readFor(std::chrono::milliseconds(100));
            if (count < 0) break;
            if (!count) continue;
            struct Flush { dsp::stream<dsp::complex_t>* input; ~Flush() { input->flush(); } } flush{input};
            std::shared_ptr<Store> saved;
            { std::lock_guard<std::mutex> lock(control); saved = store; }
            // Rate changes can precede the UI's new disk reservation. Untag those
            // frames instead of assigning samples to a buffer with another rate.
            const auto first = saved && saved->state().rate == rate ? saved->append(input->readBuf, count) : Stamp{};
            framer.push(input->readBuf, count, first, [&](dsp::complex_t* data, int n, const Stamp& stamp) {
                currentFrame = stamp; fft(data, n); currentFrame = {};
            });
        }
        } catch (const std::exception& error) {
            fault(std::string("IQ replay framing stopped: ") + error.what());
        }
    });
}
void stopFramer() {
    stopFrames = true;
    if (frameInput) frameInput->stopReader();
    if (frameWorker && frameWorker->joinable()) frameWorker->join();
    frameWorker.reset();
    if (frameInput) frameInput->clearReadStop();
    frameInput = nullptr;
}
Stamp frameStamp() { return currentFrame; }
void shutdown() {
    live(); stopFramer();
    std::shared_ptr<Store> previous;
    { std::lock_guard<std::mutex> lock(control); previous.swap(store); }
    feature = false;
}
}
