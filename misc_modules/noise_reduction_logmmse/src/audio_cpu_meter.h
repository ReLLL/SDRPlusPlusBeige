#pragma once
#include <atomic>
#include <ctime>
#ifdef _WIN32
#include <windows.h>
#endif

namespace dsp {
class AudioCpuMeter {
public:
    std::atomic<float> percent{-1};

    static double now() {
#ifdef _WIN32
        FILETIME created, exited, kernel, user;
        if (!GetThreadTimes(GetCurrentThread(), &created, &exited, &kernel, &user)) return -1;
        ULARGE_INTEGER k, u;
        k.LowPart = kernel.dwLowDateTime; k.HighPart = kernel.dwHighDateTime;
        u.LowPart = user.dwLowDateTime; u.HighPart = user.dwHighDateTime;
        return double(k.QuadPart + u.QuadPart) * 1e-7;
#elif defined(CLOCK_THREAD_CPUTIME_ID)
        timespec value{};
        if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &value) != 0) return -1;
        return value.tv_sec + value.tv_nsec * 1e-9;
#else
        return -1;
#endif
    }

    void add(double start, int samples, int rate) {
        const double end = now();
        if (start < 0 || end < start || samples <= 0 || rate <= 0) return;
        cpu_ += end - start;
        audio_ += double(samples) / rate;
        if (audio_ >= .5) {
            percent.store(float(100 * cpu_ / audio_));
            cpu_ = audio_ = 0;
        }
    }

    void reset() {
        cpu_ = audio_ = 0;
        percent.store(-1);
    }

private:
    double cpu_ = 0, audio_ = 0;
};
}
