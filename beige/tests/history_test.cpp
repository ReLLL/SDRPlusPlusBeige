#include "gui/widgets/spectrum_history.h"
#include <array>
#include <cassert>
#include <chrono>
#include <iostream>
#include <limits>

int main() {
    beige::SpectrumHistory history;
    std::array<float, 8192> fft;
    fft.fill(-95.0f);
    fft[4217] = -12.0f;
    history.push(fft.data(), fft.size(), 1000);
    assert(history.size() == 1);
    assert(*std::max_element(history.row(0).begin(), history.row(0).end()) == -12.0f);
    fft.fill(-80.0f);
    history.push(fft.data(), fft.size(), 1050);
    assert(history.size() == 1); // Front is live, even between history slices.
    assert(history.row(0)[0] == -80.0f);
    fft.fill(-70.0f);
    history.push(fft.data(), fft.size(), 1100);
    assert(history.size() == 2 && history.ageMs(0) == 0);
    assert(history.row(0)[0] == -70.0f && history.row(1)[0] == -80.0f);
    assert(history.ageMs(1) == 50);
    for (int i = 2; i < 1000; ++i) {
        fft.fill((float)-i);
        history.push(fft.data(), fft.size(), 1000 + i * 100);
    }
    assert(history.size() == history.capacity);
    assert(history.ageMs(0) == 0 && history.ageMs(history.capacity - 1) == history.durationMs);
    history.push(fft.data(), fft.size(), 102000);
    assert(history.ageMs(history.size() - 1) <= history.durationMs);
    history.push(fft.data(), fft.size(), 120000);
    assert(history.size() == 1); // Resume after a gap must not join unrelated time spans.
    history.push(fft.data(), fft.size(), 1);
    assert(history.size() == 1); // Clock reversal.
    history.clear();
    fft.fill(std::numeric_limits<float>::quiet_NaN());
    history.push(fft.data(), fft.size(), 1000);
    assert(std::isfinite(history.row(0)[0]));
    float one = -42;
    history.push(&one, 1, 1100);
    assert(history.row(0).front() == one && history.row(0).back() == one);
    const auto front = beige::projectSpectrum(0.5f, 0, 0);
    const auto older = beige::projectSpectrum(0.5f, 1, 0);
    assert(older.y < front.y);
    assert(beige::projectSpectrum(0.5f, 0, 1).y < front.y);
    assert(beige::projectSpectrum(1, 0, 0).x == 1);
    assert(beige::projectSpectrum(0, 0, 0).x == 0);
    std::cout << "History chronology, capacity, peak preservation, gaps, invalid samples and projection passed.\n";
    fft.fill(-90);
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < 10000; ++i) { history.push(fft.data(), fft.size(), 2000 + i * 50); }
    const auto us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count();
    std::cout << "8192-bin FFT ingestion: " << us / 10000 << " us/update; fixed history storage: " << sizeof(history) << " bytes.\n";
}
