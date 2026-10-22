#include "audio/rewind_buffer.h"
#include <array>
#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>

using beige::rewind::Buffer;
using dsp::stereo_t;

int main() {
    Buffer buffer;
    constexpr int rate = 8000;
    buffer.configure(rate);
    std::array<stereo_t, 257> input{}, output{};
    uint64_t counter = 0;
    auto push = [&](size_t count) {
        for (size_t i = 0; i < count; ++i) {
            input[i] = {float(counter + i), -float(counter + i)};
        }
        buffer.process(input.data(), output.data(), count);
        counter += count;
    };
    while (counter < rate * 75) {
        push(input.size());
        assert(std::memcmp(input.data(), output.data(), sizeof(input)) == 0); // Live is bit-identical, including stereo.
    }
    auto at = buffer.state();
    assert(at.available == 60 && at.bytes == rate * 65 * sizeof(stereo_t));
    buffer.seek(10);
    const uint64_t cursor = counter - rate * 10;
    push(input.size());
    for (size_t i = rate / 200; i < output.size(); ++i) {
        assert(output[i].l == cursor + i && output[i].r == -float(cursor + i));
    }
    buffer.setPaused(true);
    const double delay = buffer.state().delay;
    push(input.size());
    for (size_t i = rate / 200; i < output.size(); ++i) assert(output[i].l == 0 && output[i].r == 0);
    assert(buffer.state().delay > delay && buffer.state().paused);
    buffer.setPaused(false);
    buffer.renderStopped(output.data(), output.size());
    for (size_t i = rate / 200; i < output.size(); ++i) assert(output[i].l == cursor + input.size() + i);

    at = buffer.state();
    for (int i = 0; i < 20; ++i) push(input.size());
    auto clip = buffer.snapshot(at, 60);
    assert(clip.rate == rate && clip.samples.size() == rate * 60);
    assert(clip.samples.front().l == at.head - rate * 60);
    assert(clip.samples.back().l == at.head - 1); // Save ends at the click, not after worker startup.
    auto shortClip = buffer.snapshot(at, 30);
    assert(shortClip.samples.size() == rate * 30);

    buffer.setPaused(true);
    for (int i = 0; i < 2000; ++i) push(input.size());
    assert(buffer.state().delay == 60 && buffer.state().paused); // Bounded pause retention.
    buffer.seek(1);
    buffer.setPaused(false);
    for (int i = 0; i < 40; ++i) buffer.renderStopped(output.data(), output.size());
    assert(buffer.state().paused && buffer.state().delay == 0); // Offline replay ends safely.

    buffer.goLive();
    push(input.size());
    push(input.size());
    assert(std::memcmp(input.data(), output.data(), sizeof(input)) == 0);
    at = buffer.state();
    buffer.configure(48000);
    assert(buffer.state().head == 0 && buffer.snapshot(at, 30).samples.empty());
    buffer.configure(0);
    push(input.size());
    assert(buffer.state().bytes == 0 && std::memcmp(input.data(), output.data(), sizeof(input)) == 0);
    buffer.configure(1000000);
    assert(buffer.state().bytes == 0); // Unsupported raw rates never allocate unbounded RAM.

    buffer.configure(48000);
    std::array<stereo_t, 480> audio{}, result{};
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < 20000; ++i) buffer.process(audio.data(), result.data(), audio.size());
    const auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::thread writer([&] { for (int i = 0; i < 1000; ++i) buffer.process(audio.data(), result.data(), audio.size()); });
    for (int i = 0; i < 10; ++i) {
        auto copy = buffer.snapshot(buffer.state(), 30);
        assert(copy.rate == 48000 && copy.samples.size() == 48000 * 30);
    }
    writer.join();
    std::cout << "Stereo parity, ring wrap, seek, pause retention, stopped replay, click-time exports, rates and concurrency passed.\n"
              << "48 kHz history wall processing time per audio-second: " << seconds / 200 * 100 << "%\n";
}
