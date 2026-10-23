#include "rewind.h"
#include <dsp/processor.h>
#include <signal_path/signal_path.h>
#include <core.h>
#include <http_debug_server.h>
#include <iq_replay/replay.h>
#include <filesystem>
#include <fstream>
#include <future>
#include <map>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <cctype>
#include "../../../decoder_modules/radio/src/radio_interface.h"
#ifdef __APPLE__
#include <spawn.h>
#include <sys/wait.h>
extern char** environ;
#endif

namespace beige::rewind {
namespace {
std::atomic<bool> receiving{false};
std::atomic<void*> playbackKey{nullptr};
class Capture : public dsp::Processor<dsp::stereo_t, dsp::stereo_t> {
public:
    void* key = nullptr;
    std::shared_ptr<Buffer> history = std::make_shared<Buffer>();
    int sampleRate;
    bool audioMode = true;
    explicit Capture(dsp::stream<dsp::stereo_t>* input, int rate) : sampleRate(rate) {
        out.setBufferSize(4096);
        init(input);
    }
    ~Capture() override { stop(); }
    int run() override {
        auto current = history->state();
        const bool offline = !receiving && ((current.head && current.rate) || iqreplay::listeningActive(key));
        int count = offline && !_in->isDataReady() ? 0 : _in->readFor(std::chrono::milliseconds(100));
        if (count < 0) return -1;
        if (count > 0) {
            for (int offset = 0; offset < count; offset += 4096) {
                const int chunk = std::min(4096, count - offset);
                history->process(_in->readBuf + offset, out.writeBuf, chunk);
                routeIQ(chunk);
                if (!out.swap(chunk)) { _in->flush(); return -1; }
            }
            _in->flush();
            return count;
        }
        else if (offline) {
            count = std::max(1, sampleRate / 100);
            history->renderStopped(out.writeBuf, count);
            routeIQ(count);
            const auto now = std::chrono::steady_clock::now();
            nextTick = std::max(nextTick, now) + std::chrono::milliseconds(10);
            std::this_thread::sleep_until(nextTick);
        }
        return count && !out.swap(count) ? -1 : count;
    }
private:
    void routeIQ(int count) {
        const bool active = iqreplay::listeningActive(key) && iqreplay::renderAudio(key, out.writeBuf, count, history->state().live);
        if (active != wasIQ) { wasIQ = active; fadeRemaining = std::max(1, sampleRate / 200); fadeStart = last; }
        const int length = std::max(1, sampleRate / 200);
        for (int i = 0; i < count && fadeRemaining; ++i, --fadeRemaining) {
            const float mix = 1 - float(fadeRemaining) / length;
            out.writeBuf[i].l = fadeStart.l * (1 - mix) + out.writeBuf[i].l * mix;
            out.writeBuf[i].r = fadeStart.r * (1 - mix) + out.writeBuf[i].r * mix;
        }
        if (count) last = out.writeBuf[count - 1];
    }
    bool wasIQ = false;
    int fadeRemaining = 0;
    dsp::stereo_t last{}, fadeStart{};
    std::chrono::steady_clock::time_point nextTick{};
};

std::map<void*, std::unique_ptr<Capture>> captures;
Capture* selected = nullptr;
std::string selectedName;
bool featureEnabled = true, initialized = false;
ExportState lastExport;
std::future<ExportState> exportJob;

void pollExport() {
    if (!exportJob.valid() || exportJob.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    try { lastExport = exportJob.get(); }
    catch (const std::exception& error) { lastExport = {false, "", error.what(), 0}; }
}

void initialize() {
    if (initialized) return;
    initialized = true;
    featureEnabled = core::configManager.conf.value("audioRewind", true);
    using namespace httpdebug::procfs;
    registerEndpoint("/audio-rewind/status", [] {
        const auto s = state();
        return json{{"stream", selectedName}, {"enabled", enabled()}, {"rate", s.rate},
            {"captured_frames", s.head}, {"available_seconds", s.available}, {"delay_seconds", s.delay},
            {"live", s.live}, {"paused", s.paused}, {"buffer_bytes", s.bytes}, {"receiving", receiving.load()}}.dump();
    }, nullptr, Type::String);
    registerEndpoint("/audio-rewind/seek", [] { return std::to_string(state().delay); }, [](const std::string& value) {
        try { seek(std::stod(value)); } catch (const std::exception&) {}
    }, Type::Float);
    registerEndpoint("/audio-rewind/paused", [] { return state().paused ? "true" : "false"; },
        [](const std::string& value) { pause(value == "true"); }, Type::Bool);
    registerEndpoint("/audio-rewind/live", [] { return state().live ? "true" : "false"; },
        [](const std::string& value) { if (value == "true") live(); }, Type::Bool);
    registerEndpoint("/audio-rewind/enabled", [] { return enabled() ? "true" : "false"; },
        [](const std::string& value) { enable(value == "true"); }, Type::Bool);
    registerEndpoint("/audio-rewind/save", [] { return "30 or 60 seconds"; }, [](const std::string& value) {
        if (value == "30" || value == "60") save(value == "30" ? 30 : 60);
    }, Type::String);
    registerEndpoint("/audio-rewind/export", [] {
        const auto e = exportState();
        return json{{"busy", e.busy}, {"path", e.path}, {"error", e.error}, {"seconds", e.seconds}}.dump();
    }, nullptr, Type::String);
}

ExportState writeClip(const std::shared_ptr<Buffer>& history, const State& at, int seconds,
        const std::filesystem::path& directory, const std::string& name) {
    auto clip = history->snapshot(at, seconds);
    if (clip.samples.empty()) return {false, "", "Audio changed before saving. Try again.", 0};
    std::filesystem::create_directories(directory);
    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);
    std::tm utc{};
#ifdef _WIN32
    gmtime_s(&utc, &time);
#else
    gmtime_r(&time, &utc);
#endif
    std::ostringstream filename;
    filename << "Audio_" << name << '_' << std::put_time(&utc, "%Y%m%dT%H%M%SZ") << '_'
        << std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000 << ".wav";
    const auto path = directory / filename.str();
    std::ofstream file(path, std::ios::binary);
    // IEEE float stereo WAV, preserving the post-NR signal without clipping.
    auto u16 = [&](uint16_t value) { char b[]{char(value), char(value >> 8)}; file.write(b, 2); };
    auto u32 = [&](uint32_t value) { char b[]{char(value), char(value >> 8), char(value >> 16), char(value >> 24)}; file.write(b, 4); };
    const uint32_t bytes = uint32_t(clip.samples.size() * sizeof(dsp::stereo_t));
    file.write("RIFF", 4); u32(48 + bytes); file.write("WAVEfmt ", 8); u32(16);
    u16(3); u16(2); u32(clip.rate); u32(clip.rate * 8); u16(8); u16(32);
    file.write("fact", 4); u32(4); u32(uint32_t(clip.samples.size()));
    file.write("data", 4); u32(bytes);
    file.write(reinterpret_cast<const char*>(clip.samples.data()), bytes);
    file.close();
    if (!file) return {false, "", "Could not save audio. Check the recordings folder and free space.", 0};
    return {false, path.string(), "", double(clip.samples.size()) / clip.rate};
}
}

dsp::stream<dsp::stereo_t>* connect(void* key, dsp::stream<dsp::stereo_t>* input, int rate) {
    auto instance = std::make_unique<Capture>(input, rate);
    instance->key = key;
    auto* output = &instance->out;
    captures[key] = std::move(instance);
    return output;
}
void start(void* key) { auto it = captures.find(key); if (it != captures.end()) it->second->start(); }
void stop(void* key) { auto it = captures.find(key); if (it != captures.end()) it->second->stop(); }
void remove(void* key) {
    auto it = captures.find(key);
    if (it == captures.end()) return;
    if (selected == it->second.get()) { playbackKey = nullptr; selected = nullptr; selectedName.clear(); }
    captures.erase(it);
}
void setRate(void* key, int rate) {
    auto it = captures.find(key);
    if (it == captures.end() || it->second->sampleRate == rate) return;
    it->second->sampleRate = rate;
    if (selected == it->second.get()) selected->history->configure(featureEnabled && selected->audioMode ? rate : 0);
}
void setReceiving(bool value) { receiving = value; }
bool playbackActive(void* key) { return key && (playbackKey.load(std::memory_order_relaxed) == key || iqreplay::listeningActive(key)); }
void select(const std::string& name) {
    initialize();
    auto stream = sigpath::sinkManager.streams.find(name);
    auto it = stream == sigpath::sinkManager.streams.end() ? captures.end() : captures.find(stream->second);
    auto* next = it == captures.end() ? nullptr : it->second.get();
    bool audioMode = true;
    if (core::modComManager.interfaceExists(name) && core::modComManager.getModuleName(name) == "radio") {
        int mode = -1;
        core::modComManager.callInterface(name, RADIO_IFACE_CMD_GET_MODE, nullptr, &mode);
        audioMode = mode != RADIO_IFACE_MODE_RAW;
    }
    if (selected == next && (!selected || selected->audioMode == audioMode)) {
        const auto s = state();
        playbackKey = s.rate && s.head ? stream->second : nullptr;
        return;
    }
    playbackKey = nullptr;
    if (selected) selected->history->configure(0);
    selected = next;
    selectedName = selected ? name : "";
    if (selected) {
        selected->audioMode = audioMode;
        selected->history->configure(featureEnabled && audioMode ? selected->sampleRate : 0);
    }
}
State state() { return selected ? selected->history->state() : State{}; }
void seek(double seconds) { if (selected) selected->history->seek(seconds); }
void pause(bool value) { if (selected) selected->history->setPaused(value); }
void current() { if (selected) selected->history->goLive(); }
void current(const std::string& receiver) {
    const auto stream = sigpath::sinkManager.streams.find(receiver);
    if (stream == sigpath::sinkManager.streams.end()) return;
    const auto it = captures.find(stream->second);
    if (it != captures.end()) it->second->history->goLive();
}
void live() { iqreplay::live(); current(); }
bool enabled() { return featureEnabled; }
void enable(bool value) {
    if (featureEnabled == value) return;
    featureEnabled = value;
    playbackKey = nullptr;
    if (selected) selected->history->configure(value && selected->audioMode ? selected->sampleRate : 0);
    core::configManager.acquire();
    core::configManager.conf["audioRewind"] = value;
    core::configManager.release(true);
}
ExportState exportState() { pollExport(); return lastExport; }
void save(int seconds) {
    pollExport();
    if (!selected || lastExport.busy || (seconds != 30 && seconds != 60)) return;
    const auto at = state();
    if (!at.head) return;
    std::string safeName = selectedName;
    for (auto& c : safeName) if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-') c = '_';
    const auto directory = std::filesystem::path(core::args["root"].s()) / "recordings";
    lastExport = {true, "", "", 0};
    try { exportJob = std::async(std::launch::async, writeClip, selected->history, at, seconds, directory, safeName); }
    catch (const std::exception&) { lastExport = {false, "", "Could not start the background save. Try again.", 0}; }
}
void reveal() {
#ifdef __APPLE__
    const auto e = exportState();
    if (e.path.empty()) return;
    char* arguments[]{const_cast<char*>("open"), const_cast<char*>("-R"), const_cast<char*>(e.path.c_str()), nullptr};
    pid_t pid;
    if (posix_spawn(&pid, "/usr/bin/open", nullptr, nullptr, arguments, environ) == 0)
        std::thread([pid] { waitpid(pid, nullptr, 0); }).detach();
#endif
}
void shutdown() {
    playbackKey = nullptr;
    if (exportJob.valid()) exportJob.wait();
    // Consumers must release the rewind output before its storage is destroyed.
    for (auto& [name, stream] : sigpath::sinkManager.streams) stream->stop();
    for (auto& [key, capture] : captures) capture->stop();
    selected = nullptr;
    captures.clear();
}
}
