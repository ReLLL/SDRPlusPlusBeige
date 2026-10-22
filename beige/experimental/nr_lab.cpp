#include <volk/volk.h>
#include "classic_nr.h"
#include "logmmse_variants.h"
#include "nr_lab_checks.h"
#include <chrono>
#include <ctime>
#include <fstream>
#include <iostream>
#include <set>

int run(int argc, char** argv) {
    if (argc == 2 && std::string(argv[1]) == "--self-test") return beige::research::labChecks();
    if (argc < 6) throw std::runtime_error("Usage: nr_lab input.cf32 output.cf32 rate variant [apple] [block]");
    const int rate = std::stoi(argv[3]);
    const std::string mode = argv[4];
    const std::set<std::string> modes{"reference", "clone", "cap", "fast", "smooth", "bias", "bootstrap",
        "tracker", "gh", "wiener", "sqrtwiener", "balanced", "framewise", "framewiener", "blend95", "blend85"};
    if (!modes.count(mode)) throw std::runtime_error("Unknown research variant");
    const bool apple = std::stoi(argv[5]) != 0;
    const int chunk = argc > 6 ? std::stoi(argv[6]) : rate / 200;
    if (rate < 48000 || rate > 2400000 || chunk < 1 || chunk > rate / 4) throw std::runtime_error("Invalid rate/block");
    std::ifstream file(argv[1], std::ios::binary | std::ios::ate);
    if (!file) throw std::runtime_error("Cannot read IQ input");
    const auto size = file.tellg();
    if (size <= 0 || size % sizeof(dsp::complex_t)) throw std::runtime_error("Invalid complex float input size");
    std::vector<dsp::complex_t> input(size_t(size) / sizeof(dsp::complex_t)), output, block(1000000);
    file.seekg(0); file.read(reinterpret_cast<char*>(input.data()), size);
    dsp::ClassicNR reference;
    reference.configure(rate, apple);
    beige::research::LogMMSEVariant candidate(rate, apple, mode);
    double elapsed = 0, cpu = 0;
    for (size_t at = 0; at < input.size();) {
        const int count = int((std::min)(size_t(chunk), input.size() - at));
        const auto start = std::chrono::steady_clock::now();
        const auto cpuStart = std::clock();
        const bool blend = mode == "blend95" || mode == "blend85";
        const int written = mode == "reference" || blend ? reference.process(input.data() + at, count, block.data())
            : candidate.process(input.data() + at, count, block.data());
        cpu += double(std::clock() - cpuStart) / CLOCKS_PER_SEC;
        elapsed += std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        for (int i = 0; i < written; ++i) {
            if (!std::isfinite(block[i].re) || !std::isfinite(block[i].im)) throw std::runtime_error("Nonfinite output");
            // Remove only the established wrapper's 4x boost for fair scoring.
            block[i] *= 0.25f;
            if (blend) {
                const float wet = mode == "blend95" ? 0.95f : 0.85f;
                const auto dry = input[output.size() + i];
                block[i] = {block[i].re * wet + dry.re * (1 - wet), block[i].im * wet + dry.im * (1 - wet)};
            }
        }
        output.insert(output.end(), block.begin(), block.begin() + written);
        at += count;
    }
    std::ofstream out(argv[2], std::ios::binary);
    out.write(reinterpret_cast<const char*>(output.data()), output.size() * sizeof(dsp::complex_t));
    if (!out) throw std::runtime_error("Cannot write IQ output");
    std::cout << "{\"mode\":\"" << mode << "\",\"rate\":" << rate
        << ",\"samples\":" << output.size() << ",\"wall_seconds\":" << elapsed
        << ",\"cpu_seconds\":" << cpu << ",\"cpu_percent\":" << cpu * rate / input.size() * 100 << "}\n";
    return 0;
}

int main(int argc, char** argv) {
    int code = 0;
    try { code = run(argc, argv); }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; code = 1; }
    std::cout.flush(); std::cerr.flush();
    // Core global services need GUI-owned shutdown; all local state is gone.
    std::_Exit(code);
}
