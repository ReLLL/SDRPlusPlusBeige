#include <gui/widgets/spectrum_signal.h>
#include <cassert>
#include <limits>
#include <thread>

int main() {
    // A single FFT allocation makes a one-past-row read observable under ASAN.
    std::vector<float> fft(65536);
    for (size_t i = 0; i < fft.size(); ++i) fft[i] = -110 + float(i % 31) / 10;
    float strength = 17, snr = 23;
    assert(beige::spectrumSignalInfo(fft.data(), fft.size(), 800000, 300000, 250000, strength, snr));
    assert(std::isfinite(strength) && std::isfinite(snr));
    assert(beige::spectrumSignalInfo(fft.data(), fft.size(), 800000, -300000, 250000, strength, snr));
    assert(!beige::spectrumSignalInfo(fft.data(), fft.size(), 800000, 600000, 200000, strength, snr));
    assert(!beige::spectrumSignalInfo(fft.data(), fft.size(), 800000, -600000, 200000, strength, snr));
    for (double center = -450000; center <= 450000; center += 5000)
        for (double bandwidth : {1., 100., 3000., 250000., 800000., 1600000.})
            beige::spectrumSignalInfo(fft.data(), fft.size(), 800000, center, bandwidth, strength, snr);
    for (double invalid : {0., -1., std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
        assert(!beige::spectrumSignalInfo(fft.data(), fft.size(), invalid, 0, 3000, strength, snr));
        assert(!beige::spectrumSignalInfo(fft.data(), fft.size(), 800000, 0, invalid, strength, snr));
    }
    assert(!beige::spectrumSignalInfo(nullptr, 65536, 800000, 0, 3000, strength, snr));
    assert(!beige::spectrumSignalInfo(fft.data(), 0, 800000, 0, 3000, strength, snr));
    auto run = [&] {
        for (int i = 0; i < 100; ++i) {
            float s, n;
            assert(beige::spectrumSignalInfo(fft.data(), fft.size(), 800000, 0, 3000, s, n));
            assert(std::isfinite(s) && std::isfinite(n));
        }
    };
    std::thread other(run);
    run();
    other.join();
}
