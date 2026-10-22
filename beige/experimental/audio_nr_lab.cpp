// Offline research only. Does not open a receiver or change its settings.
#include "af_nr.h"
#ifdef BEIGE_NEURAL_AUDIO
#include "neural_audio.h"
#endif
#include <algorithm>
#include <cmath>
#include <ctime>
#include <dlfcn.h>
#include <fstream>
#include <iostream>
#include <mach/mach.h>
#include <string>
#include <vector>

char* sdrppResourcesDirectory = nullptr;
// Exercise the production wrapper without loading GUI/core global objects.
namespace sigpath { Event<bool> txState; }
namespace flog {
std::string __toString__(const char* value) { return value; }
std::string __toString__(int value) { return std::to_string(value); }
std::string formatString(const char* message, const std::vector<std::string>&) { return message; }
void __log__(Type, const char* message, const std::vector<std::string>&) {
    std::cerr << message << '\n';
}
}

static size_t residentBytes() {
    mach_task_basic_info_data_t info{};
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO,
            reinterpret_cast<task_info_t>(&info), &count) != KERN_SUCCESS) return 0;
    return info.resident_size;
}

template<typename T> static T symbol(void* library, const char* name) {
    auto pointer = dlsym(library, name);
    if (!pointer) throw std::runtime_error(name);
    return reinterpret_cast<T>(pointer);
}

int main(int argc, char** argv) {
    if (argc < 5) {
        std::cerr << "audio_nr_lab ENGINE INPUT.f32 OUTPUT.f32 RESOURCES [CHUNK=1984] [REPEAT=1] [LIBRARY] [MODEL]\n";
        return 2;
    }
    std::string resources = argv[4];
    sdrppResourcesDirectory = resources.data();
    const std::string method = argv[1];
    const int chunk = argc > 5 ? std::stoi(argv[5]) : 1984;
    const int repeats = argc > 6 ? std::stoi(argv[6]) : 1;
    if (chunk <= 0 || chunk > 8192 || repeats <= 0) return 2;
    if (method == "memory") {
        dsp::omlsa_mcra engine;
        engine.setSampleRate(48000);
        const auto before = residentBytes();
        for (int i = 0; i < 20; ++i) engine.reset();
        const auto after = residentBytes();
        std::cout << "{\"reset_count\":20,\"resident_before\":" << before
                  << ",\"resident_after\":" << after << "}\n";
        return 0;
    }
    std::ifstream source(argv[2], std::ios::binary | std::ios::ate);
    if (!source) return 2;
    const size_t n = source.tellg() / sizeof(float);
    source.seekg(0);
    std::vector<float> input(n);
    source.read(reinterpret_cast<char*>(input.data()), n * sizeof(float));
    std::vector<float> output;
    const auto initStart = std::clock();
    std::unique_ptr<dsp::AFNR_OMLSA_MCRA> classic;
#ifdef BEIGE_NEURAL_AUDIO
    std::unique_ptr<dsp::NeuralVoiceEngine> gtcrn;
    if (method == "gtcrn") {
        if (argc < 9) return 2;
        gtcrn = std::make_unique<dsp::NeuralVoiceEngine>(argv[8], 48000, true);
    }
#endif
    void* library = nullptr;
    void* neural = nullptr;
    float (*process)(void*, float*, const float*) = nullptr;
    float (*dfProcess)(void*, float*, float*) = nullptr;
    void (*destroy)(void*) = nullptr;
    int frame = chunk;
    if (method == "omlsa") {
        classic = std::make_unique<dsp::AFNR_OMLSA_MCRA>();
        classic->omlsa_mcra.setSampleRate(48000);
        classic->allowed = true;
    } else if (method == "rnnoise" || method == "deepfilter") {
        if (argc < 8 || !(library = dlopen(argv[7], RTLD_NOW | RTLD_LOCAL))) return 2;
        if (method == "rnnoise") {
            neural = symbol<void* (*)(void*)>(library, "rnnoise_create")(nullptr);
            frame = symbol<int (*)()>(library, "rnnoise_get_frame_size")();
            process = symbol<float (*)(void*, float*, const float*)>(library, "rnnoise_process_frame");
            destroy = symbol<void (*)(void*)>(library, "rnnoise_destroy");
        } else {
            if (argc < 9) return 2;
            neural = symbol<void* (*)(const char*, float, const char*)>(library, "df_create")(argv[8], 100.0f, nullptr);
            frame = symbol<size_t (*)(void*)>(library, "df_get_frame_length")(neural);
            dfProcess = symbol<float (*)(void*, float*, float*)>(library, "df_process_frame");
            destroy = symbol<void (*)(void*)>(library, "df_free");
        }
        if (!neural || frame <= 0 || frame > 8192) return 2;
    } else if (method != "gtcrn") return 2;
    const double initCpu = double(std::clock() - initStart) / CLOCKS_PER_SEC;
    std::vector<dsp::stereo_t> stereo(chunk), denoised(32768);
    std::vector<float> block(frame), wet(frame);
    double processingCpu = 0;
    size_t totalWritten = 0;
    for (int repeat = 0; repeat < repeats; ++repeat) {
        for (size_t offset = 0; offset < n; offset += frame) {
            const int count = std::min<size_t>(frame, n - offset);
            if (classic
#ifdef BEIGE_NEURAL_AUDIO
                || gtcrn
#endif
            ) {
                for (int i = 0; i < count; ++i) stereo[i].l = stereo[i].r = input[offset + i];
                int wrote = -1;
                const auto start = std::clock();
                if (classic) classic->process(stereo.data(), count, denoised.data(), wrote);
#ifdef BEIGE_NEURAL_AUDIO
                else { gtcrn->process(stereo.data(), count, denoised.data()); wrote = count; }
#endif
                processingCpu += double(std::clock() - start) / CLOCKS_PER_SEC;
                if (wrote < 0 || wrote > int(denoised.size())) return 3;
                for (int i = 0; i < wrote; ++i) {
                    if (!std::isfinite(denoised[i].l)) return 3;
                    if (repeat == 0) output.push_back(denoised[i].l);
                }
                totalWritten += wrote;
            } else {
                const float scale = process ? 32768.0f : 1.0f;
                std::fill(block.begin(), block.end(), 0);
                for (int i = 0; i < count; ++i) block[i] = input[offset + i] * scale;
                const auto start = std::clock();
                if (process) process(neural, wet.data(), block.data());
                else dfProcess(neural, block.data(), wet.data());
                processingCpu += double(std::clock() - start) / CLOCKS_PER_SEC;
                for (auto value : wet) {
                    if (!std::isfinite(value)) return 3;
                    if (repeat == 0) output.push_back(value / scale);
                }
                totalWritten += frame;
            }
        }
    }
    std::ofstream destination(argv[3], std::ios::binary);
    destination.write(reinterpret_cast<const char*>(output.data()), output.size() * sizeof(float));
    if (destroy) destroy(neural);
    if (library) dlclose(library);
    std::cout << "{\"method\":\"" << method << "\",\"input_samples\":" << n
              << ",\"output_samples\":" << output.size() << ",\"repeats\":" << repeats
              << ",\"processing_cpu_seconds\":" << processingCpu
              << ",\"initialization_cpu_seconds\":" << initCpu
              << ",\"one_core_percent\":" << processingCpu / (double(n) * repeats / 48000) * 100
              << ",\"frame_samples\":" << frame << ",\"total_written\":" << totalWritten << "}\n";
}
