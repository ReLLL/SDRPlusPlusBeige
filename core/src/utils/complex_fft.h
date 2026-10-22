#pragma once

#include <dsp/types.h>
#include <fftw3.h>
#include <algorithm>
#include <mutex>
#include <stdexcept>
#include <vector>
#if defined(__APPLE__) && !defined(SDRPP_NO_ACCELERATE)
#include <Accelerate/Accelerate.h>
#endif

namespace dsp::fft {

// One worker owns a transform. All storage and plans are prepared off the hot path.
// FFTW supports arbitrary lengths; Apple's radix-2 API requires powers of two.
class ComplexFFT {
public:
    ComplexFFT(int size, bool inverse, bool preferApple) : size_(size), inverse_(inverse) {
        if (size <= 0) throw std::invalid_argument("FFT length must be positive");
#if defined(__APPLE__) && !defined(SDRPP_NO_ACCELERATE)
        if (preferApple && (size & (size - 1)) == 0) {
            for (int n = size; n > 1; n >>= 1) ++log2_;
            setup_ = vDSP_create_fftsetup(log2_, FFT_RADIX2);
            if (setup_) {
                real_.resize(size);
                imag_.resize(size);
                return;
            }
        }
#endif
        scratch_.resize(size);
        std::lock_guard<std::mutex> lock(plannerMutex());
        plan_ = fftwf_plan_dft_1d(size, reinterpret_cast<fftwf_complex*>(scratch_.data()),
            reinterpret_cast<fftwf_complex*>(scratch_.data()), inverse ? FFTW_BACKWARD : FFTW_FORWARD,
            FFTW_ESTIMATE | FFTW_UNALIGNED);
        if (!plan_) throw std::runtime_error("Unable to create FFT plan");
    }

    ~ComplexFFT() {
#if defined(__APPLE__) && !defined(SDRPP_NO_ACCELERATE)
        if (setup_) vDSP_destroy_fftsetup(setup_);
#endif
        if (plan_) {
            std::lock_guard<std::mutex> lock(plannerMutex());
            fftwf_destroy_plan(plan_);
        }
    }
    ComplexFFT(const ComplexFFT&) = delete;
    ComplexFFT& operator=(const ComplexFFT&) = delete;

    bool usesApple() const {
#if defined(__APPLE__) && !defined(SDRPP_NO_ACCELERATE)
        return setup_ != nullptr;
#else
        return false;
#endif
    }

    void execute(complex_t* data) {
#if defined(__APPLE__) && !defined(SDRPP_NO_ACCELERATE)
        if (setup_) {
            DSPSplitComplex split{real_.data(), imag_.data()};
            vDSP_ctoz(reinterpret_cast<const DSPComplex*>(data), 2, &split, 1, size_);
            vDSP_fft_zip(setup_, &split, 1, log2_, inverse_ ? FFT_INVERSE : FFT_FORWARD);
            if (inverse_) {
                const float scale = 1.0f / size_;
                vDSP_vsmul(split.realp, 1, &scale, split.realp, 1, size_);
                vDSP_vsmul(split.imagp, 1, &scale, split.imagp, 1, size_);
            }
            vDSP_ztoc(&split, 1, reinterpret_cast<DSPComplex*>(data), 2, size_);
            return;
        }
#endif
        fftwf_execute_dft(plan_, reinterpret_cast<fftwf_complex*>(data), reinterpret_cast<fftwf_complex*>(data));
        if (inverse_) {
            const float scale = 1.0f / size_;
            for (int i = 0; i < size_; ++i) data[i] *= scale;
        }
    }

    void power(const complex_t* data, float* output) const {
#if defined(__APPLE__) && !defined(SDRPP_NO_ACCELERATE)
        if (setup_) {
            DSPSplitComplex split{const_cast<float*>(&data[0].re), const_cast<float*>(&data[0].im)};
            vDSP_zvmags(&split, 2, output, 1, size_);
            return;
        }
#endif
        for (int i = 0; i < size_; ++i) output[i] = data[i].re * data[i].re + data[i].im * data[i].im;
    }

    void applyGain(complex_t* data, const float* gain) const {
#if defined(__APPLE__) && !defined(SDRPP_NO_ACCELERATE)
        if (setup_) {
            vDSP_vmul(&data[0].re, 2, gain, 1, &data[0].re, 2, size_);
            vDSP_vmul(&data[0].im, 2, gain, 1, &data[0].im, 2, size_);
            return;
        }
#endif
        for (int i = 0; i < size_; ++i) data[i] *= gain[i];
    }

private:
    static std::mutex& plannerMutex() { static std::mutex mutex; return mutex; }
    int size_;
    bool inverse_;
    fftwf_plan plan_ = nullptr;
    std::vector<complex_t> scratch_;
#if defined(__APPLE__) && !defined(SDRPP_NO_ACCELERATE)
    FFTSetup setup_ = nullptr;
    vDSP_Length log2_ = 0;
    std::vector<float> real_, imag_;
#endif
};
}
