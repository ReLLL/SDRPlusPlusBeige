#include <iq_replay/store.h>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <thread>

using namespace beige::iqreplay;
namespace {
struct Faults { std::atomic<bool> read{false}, write{false}; std::atomic<int> open{0}; };
class MemoryFile final : public Backing {
public:
    MemoryFile(uint64_t bytes, std::shared_ptr<Faults> faults) : data(bytes), faults(std::move(faults)) { ++this->faults->open; }
    ~MemoryFile() override { --faults->open; }
    void write(uint64_t at, const void* p, size_t n) override {
        if (faults->write) throw std::runtime_error("disk full");
        assert(at + n <= data.size()); std::memcpy(data.data() + at, p, n);
    }
    void read(uint64_t at, void* p, size_t n) override {
        if (faults->read) throw std::runtime_error("disk read failed");
        assert(at + n <= data.size()); std::memcpy(p, data.data() + at, n);
    }
private:
    std::vector<char> data;
    std::shared_ptr<Faults> faults;
};
template<class F> void wait(F ready) {
    for (int i = 0; i < 10000; ++i) { if (ready()) return; std::this_thread::sleep_for(std::chrono::microseconds(100)); }
    assert(false && "IQ worker timed out");
}
Store::Factory memory(std::shared_ptr<Faults> faults) {
    return [faults](uint64_t bytes) { return std::make_unique<MemoryFile>(bytes, faults); };
}
}
int main() {
    auto faults = std::make_shared<Faults>();
    std::vector<dsp::complex_t> signal(125);
    for (size_t i = 0; i < signal.size(); ++i) signal[i] = {float(i) / 7, -float(i) / 11};
    {
        Store store(100, 7100000, 123000000, memory(faults), 1);
        wait([&] { return store.state().ready; });
        const auto first = store.append(signal.data(), 40);
        assert(first && first.sample == 0 && first.rate == 100 && first.center == 7100000);
        wait([&] { return store.state().head == 40; });
        std::vector<dsp::complex_t> output(125);
        assert(store.read(first, output.data(), 100) == 40);
        assert(std::memcmp(output.data(), signal.data(), 40 * sizeof(signal[0])) == 0);
        const auto middle = store.append(signal.data() + 40, 60);
        wait([&] { return store.state().head == 100; });
        store.append(signal.data() + 100, 25);
        wait([&] { return store.state().head == 125; });
        assert(store.state().oldest() == 25);
        assert(store.read(first, output.data(), 10) == 0); // Expired points are never silently moved.
        assert(store.read(middle, output.data(), 100) == 85);
        assert(std::memcmp(output.data(), signal.data() + 40, 85 * sizeof(signal[0])) == 0);
        store.reset(14000000, 456000000);
        assert(store.read(middle, output.data(), 10) == 0);
        const auto next = store.append(signal.data(), 8);
        assert(next.epoch != first.epoch && next.sample == 0 && next.center == 14000000);
        wait([&] { return store.state().head == 8; });
        // No normalization, clipping or precision loss in the backing format.
        signal[0] = {100.25f, -2000.5f};
        signal[1] = {std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()};
        auto exact = store.append(signal.data(), 2);
        wait([&] { return store.state().head == 10; });
        assert(store.read(exact, output.data(), 2) == 2);
        assert(std::memcmp(output.data(), signal.data(), 2 * sizeof(signal[0])) == 0);
    }
    assert(faults->open == 0);
    {
        Store store(1024, 0, 0, memory(faults), 1);
        wait([&] { return store.state().ready; });
        std::atomic<bool> done{false};
        std::thread reader([&] {
            dsp::complex_t output[96];
            while (!done) {
                const auto s = store.state();
                if (s.head < 96) continue;
                Stamp at{s.epoch, s.head - 96, s.rate, s.center, s.utcNs};
                const size_t n = store.read(at, output, 96);
                for (size_t j = 0; j < n; ++j) {
                    assert(output[j].re == float(at.sample + j));
                    assert(output[j].im == float(at.epoch));
                }
            }
        });
        for (int i = 0; i < 2000; ++i) {
            if (i % 31 == 0) store.reset(i, i);
            const auto s = store.state();
            dsp::complex_t packet[64];
            for (size_t j = 0; j < 64; ++j) packet[j] = {float(s.captured + j), float(s.epoch)};
            assert(store.append(packet, 64));
            if (i % 10 == 0) wait([&] { return store.state().queuedBytes == 0; });
        }
        done = true; reader.join();
        assert(store.state().error.empty());
    }
    {
        Store store(100, 0, 0, memory(faults), 1, 8 * sizeof(dsp::complex_t));
        wait([&] { return store.state().ready; });
        assert(!store.append(signal.data(), 9));
        wait([&] { return !store.state().error.empty() && faults->open == 0; });
        assert(!store.state().ready && store.state().head == 0);
    }
    {
        faults->write = true;
        Store store(100, 0, 0, memory(faults), 1);
        wait([&] { return store.state().ready; }); store.append(signal.data(), 2);
        wait([&] { return !store.state().error.empty() && faults->open == 0; });
        assert(!store.state().ready); faults->write = false;
    }
    {
        Store store(100, 0, 0, memory(faults), 1);
        wait([&] { return store.state().ready; }); const auto at = store.append(signal.data(), 2);
        wait([&] { return store.state().head == 2; }); faults->read = true;
        dsp::complex_t output[2]; assert(store.read(at, output, 2) == 0);
        assert(!store.state().ready && !store.state().error.empty()); faults->read = false;
    }
    {
        Store store(100, 0, 0, [](uint64_t) -> std::unique_ptr<Backing> { throw std::runtime_error("allocation failed"); });
        wait([&] { return !store.state().error.empty(); });
        assert(!store.state().ready && !store.state().starting);
    }
#ifdef __APPLE__
    const auto folder = std::filesystem::temp_directory_path() / ("beige-iq-store-" + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    {
        Store store(1000, 0, 0, [folder](uint64_t bytes) { return temporaryBacking(folder, bytes, 0); }, 1);
        wait([&] { return store.state().ready || !store.state().error.empty(); }); assert(store.state().ready);
        assert(std::filesystem::is_empty(folder)); // Delete-on-close also survives abnormal process exit.
        auto at = store.append(signal.data(), 125); wait([&] { return store.state().head == 125; });
        dsp::complex_t output[125]; assert(store.read(at, output, 125) == 125);
        assert(std::memcmp(output, signal.data(), sizeof(output)) == 0);
    }
    assert(std::filesystem::is_empty(folder));
    {
        Store store(1000, 0, 0, [folder](uint64_t bytes) {
            return temporaryBacking(folder, bytes, std::numeric_limits<uint64_t>::max());
        });
        wait([&] { return !store.state().error.empty(); }); assert(!store.state().ready);
        assert(std::filesystem::is_empty(folder));
    }
    std::filesystem::remove(folder);
#endif
    return 0;
}
