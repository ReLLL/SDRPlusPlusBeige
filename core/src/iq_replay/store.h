#pragma once
#include <dsp/types.h>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace beige::iqreplay {
struct Stamp {
    uint64_t epoch = 0, sample = 0;
    double rate = 0, center = 0;
    int64_t utcNs = 0;
    explicit operator bool() const { return epoch && rate > 0; }
};

struct StoreState {
    bool ready = false, starting = true;
    uint64_t epoch = 1, captured = 0, head = 0, capacity = 0, queuedBytes = 0;
    double rate = 0, center = 0;
    int64_t utcNs = 0;
    std::string error;
    uint64_t oldest() const { return head > capacity ? head - capacity : 0; }
};

// An explicit I/O seam lets the real queue/ring tests inject disk failures.
class Backing {
public:
    virtual ~Backing() = default;
    virtual void write(uint64_t byteOffset, const void* data, size_t bytes) = 0;
    virtual void read(uint64_t byteOffset, void* data, size_t bytes) = 0;
};
std::unique_ptr<Backing> temporaryBacking(const std::filesystem::path& directory,
    uint64_t bytes, uint64_t reserveBytes = 1024ULL * 1024 * 1024);

class Store {
public:
    using Factory = std::function<std::unique_ptr<Backing>(uint64_t)>;
    Store(double rate, double center, int64_t utcNs, Factory factory,
          double seconds = 120, size_t maxQueueBytes = 16 * 1024 * 1024);
    ~Store();
    Store(const Store&) = delete;
    Store& operator=(const Store&) = delete;
    StoreState state() const;
    Stamp append(const dsp::complex_t* data, size_t count);
    size_t read(const Stamp& at, dsp::complex_t* output, size_t count);
    void reset(double center, int64_t utcNs);
    void stop();
private:
    struct Packet { uint64_t epoch, first; std::vector<dsp::complex_t> data; };
    void work(Factory factory);
    void fail(const std::string& message);
    mutable std::mutex mutex;
    std::mutex ioMutex;
    std::condition_variable changed;
    std::deque<Packet> queue;
    StoreState status;
    size_t queueLimit;
    bool stopping = false;
    std::unique_ptr<Backing> backing;
    std::thread worker;
};
}
