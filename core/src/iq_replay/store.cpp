#include "store.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <system_error>
#ifdef __APPLE__
#include <fcntl.h>
#include <unistd.h>
#endif

namespace beige::iqreplay {
namespace {
std::atomic<uint64_t> nextEpoch{1};
#ifdef __APPLE__
class TemporaryFile final : public Backing {
public:
    TemporaryFile(const std::filesystem::path& directory, uint64_t bytes, uint64_t reserve) {
        location = directory; minimumFree = reserve;
        std::filesystem::create_directories(directory);
        const auto space = std::filesystem::space(directory);
        if (reserve > space.available || bytes > space.available - reserve)
            throw std::runtime_error("Not enough free disk space for the visible waterfall's IQ history. Waterfall replay was turned off.");
        std::string name = (directory / "iq-replay-XXXXXX").string();
        fd = mkstemp(name.data());
        if (fd < 0) throw std::system_error(errno, std::generic_category(), "Create temporary IQ buffer");
        // No pathname survives: the kernel reclaims the buffer even after a crash.
        if (unlink(name.c_str()) != 0) {
            const int error = errno;
            close(fd); fd = -1;
            std::filesystem::remove(name);
            throw std::system_error(error, std::generic_category(), "Make IQ buffer temporary");
        }
        fstore_t allocation{};
        allocation.fst_flags = F_ALLOCATECONTIG;
        allocation.fst_posmode = F_PEOFPOSMODE;
        allocation.fst_length = off_t(bytes);
        if (fcntl(fd, F_PREALLOCATE, &allocation) == -1) {
            allocation.fst_flags = F_ALLOCATEALL;
            if (fcntl(fd, F_PREALLOCATE, &allocation) == -1) {
                const int error = errno; close(fd); fd = -1;
                throw std::system_error(error, std::generic_category(), "Reserve waterfall IQ buffer");
            }
        }
        if (ftruncate(fd, off_t(bytes)) != 0) {
            const int error = errno; close(fd); fd = -1;
            throw std::system_error(error, std::generic_category(), "Size temporary IQ buffer");
        }
    }
    ~TemporaryFile() override { if (fd >= 0) close(fd); }
    void write(uint64_t offset, const void* data, size_t bytes) override {
        const auto now = std::chrono::steady_clock::now();
        if (now - lastSpaceCheck >= std::chrono::seconds(2)) {
            lastSpaceCheck = now;
            if (std::filesystem::space(location).available < minimumFree)
                throw std::runtime_error("Free disk space is low. Waterfall replay was turned off.");
        }
        transfer(offset, const_cast<void*>(data), bytes, true);
    }
    void read(uint64_t offset, void* data, size_t bytes) override { transfer(offset, data, bytes, false); }
private:
    void transfer(uint64_t offset, void* data, size_t bytes, bool writing) {
        auto* p = static_cast<char*>(data);
        while (bytes) {
            const auto done = writing ? pwrite(fd, p, bytes, off_t(offset)) : pread(fd, p, bytes, off_t(offset));
            if (done < 0 && errno == EINTR) continue;
            if (done <= 0) throw std::system_error(done < 0 ? errno : EIO,
                std::generic_category(), writing ? "Write IQ buffer" : "Read IQ buffer");
            bytes -= size_t(done); p += done; offset += uint64_t(done);
        }
    }
    int fd = -1;
    std::filesystem::path location;
    uint64_t minimumFree = 0;
    std::chrono::steady_clock::time_point lastSpaceCheck{};
};
#endif
}

std::unique_ptr<Backing> temporaryBacking(const std::filesystem::path& directory, uint64_t bytes, uint64_t reserve) {
#ifdef __APPLE__
    if (bytes > uint64_t(std::numeric_limits<off_t>::max())) throw std::runtime_error("IQ buffer is too large.");
    return std::make_unique<TemporaryFile>(directory, bytes, reserve);
#else
    throw std::runtime_error("Temporary IQ replay storage is currently available on macOS only.");
#endif
}

Store::Store(double rate, double center, int64_t utcNs, Factory factory, double seconds, size_t maxQueueBytes)
    : queueLimit(maxQueueBytes) {
    if (!std::isfinite(rate) || rate <= 0 || !std::isfinite(center) || !std::isfinite(seconds) || seconds <= 0 || rate * seconds < 1 ||
        rate * seconds > double(std::numeric_limits<int64_t>::max() / sizeof(dsp::complex_t)))
        throw std::invalid_argument("Invalid IQ buffer size or sample rate.");
    status.epoch = nextEpoch.fetch_add(1);
    status.rate = rate; status.center = center; status.utcNs = utcNs;
    status.capacity = uint64_t(std::ceil(rate * seconds));
    worker = std::thread(&Store::work, this, std::move(factory));
}
Store::~Store() { stop(); }
StoreState Store::state() const { std::lock_guard<std::mutex> lock(mutex); return status; }
void Store::fail(const std::string& message) {
    std::lock_guard<std::mutex> lock(mutex);
    status.error = message; status.ready = status.starting = false;
    queue.clear(); status.queuedBytes = 0;
    changed.notify_all();
}
Stamp Store::append(const dsp::complex_t* data, size_t count) {
    std::lock_guard<std::mutex> lock(mutex);
    if (!status.ready || stopping || !count) return {};
    if (count > status.capacity || count > queueLimit / sizeof(dsp::complex_t) ||
        status.queuedBytes > queueLimit - count * sizeof(dsp::complex_t)) {
        status.error = "The disk could not keep up with IQ capture. Waterfall replay was turned off.";
        status.ready = false; queue.clear(); status.queuedBytes = 0; changed.notify_all(); return {};
    }
    Stamp at{status.epoch, status.captured, status.rate, status.center, status.utcNs};
    try { queue.push_back({status.epoch, status.captured, {data, data + count}}); }
    catch (const std::bad_alloc&) {
        status.error = "Not enough memory for the IQ write queue. Waterfall replay was turned off.";
        status.ready = false; changed.notify_all(); return {};
    }
    status.captured += count; status.queuedBytes += count * sizeof(dsp::complex_t);
    changed.notify_one(); return at;
}
size_t Store::read(const Stamp& at, dsp::complex_t* output, size_t count) {
    std::lock_guard<std::mutex> io(ioMutex);
    uint64_t capacity;
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (!backing || !status.ready || at.epoch != status.epoch || at.sample < status.oldest() || at.sample >= status.head) return 0;
        capacity = status.capacity;
        count = size_t(std::min<uint64_t>(count, status.head - at.sample));
    }
    try {
        const size_t first = size_t(std::min<uint64_t>(count, capacity - at.sample % capacity));
        backing->read((at.sample % capacity) * sizeof(*output), output, first * sizeof(*output));
        if (first < count) backing->read(0, output + first, (count - first) * sizeof(*output));
    } catch (const std::exception& error) { fail(error.what()); return 0; }
    std::lock_guard<std::mutex> lock(mutex);
    return at.epoch == status.epoch && status.ready ? count : 0;
}
void Store::reset(double center, int64_t utcNs) {
    std::lock_guard<std::mutex> lock(mutex);
    status.epoch = nextEpoch.fetch_add(1); status.center = center; status.utcNs = utcNs;
    status.captured = status.head = status.queuedBytes = 0; queue.clear();
}
void Store::stop() {
    { std::lock_guard<std::mutex> lock(mutex); stopping = true; queue.clear(); status.queuedBytes = 0; changed.notify_all(); }
    if (worker.joinable()) worker.join();
    std::lock_guard<std::mutex> io(ioMutex);
    backing.reset();
    std::lock_guard<std::mutex> lock(mutex); status.ready = status.starting = false;
}
void Store::work(Factory factory) {
    try {
        auto file = factory(status.capacity * sizeof(dsp::complex_t));
        { std::lock_guard<std::mutex> io(ioMutex); backing = std::move(file); }
        { std::lock_guard<std::mutex> lock(mutex); status.starting = false; status.ready = !stopping; }
        for (;;) {
            Packet packet;
            uint64_t capacity;
            {
                std::unique_lock<std::mutex> lock(mutex);
                changed.wait(lock, [&] { return stopping || !status.error.empty() || !queue.empty(); });
                if (stopping || !status.error.empty()) break;
                packet = std::move(queue.front()); queue.pop_front();
                status.queuedBytes -= packet.data.size() * sizeof(dsp::complex_t); capacity = status.capacity;
                if (packet.epoch != status.epoch) continue;
            }
            std::lock_guard<std::mutex> io(ioMutex);
            const size_t first = size_t(std::min<uint64_t>(packet.data.size(), capacity - packet.first % capacity));
            backing->write((packet.first % capacity) * sizeof(dsp::complex_t), packet.data.data(), first * sizeof(dsp::complex_t));
            if (first < packet.data.size()) backing->write(0, packet.data.data() + first, (packet.data.size() - first) * sizeof(dsp::complex_t));
            std::lock_guard<std::mutex> lock(mutex);
            if (packet.epoch == status.epoch && status.ready) status.head = packet.first + packet.data.size();
        }
    } catch (const std::exception& error) { fail(error.what()); }
    // Release reserved disk space on failure without waiting for a UI frame.
    std::lock_guard<std::mutex> io(ioMutex); backing.reset();
}
}
