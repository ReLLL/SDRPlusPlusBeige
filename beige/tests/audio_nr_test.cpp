#include "omlsa_mcra/Datablock_Read.h"
#include <utils/flog.h>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

char* sdrppResourcesDirectory = nullptr;
// Keep the numerical regression independent of the GUI/core runtime.
namespace flog {
std::string formatString(const char* message, const std::vector<std::string>&) { return message; }
void __log__(Type, const char* message, const std::vector<std::string>&) {
    std::cerr << message << '\n';
}
}

int main(int argc, char** argv) {
    assert(argc == 2);
    sdrppResourcesDirectory = argv[1];
    Datablock_Read denoiser(48000, 1, 10000);
    assert(denoiser.m_derr_code == 0);
    const int count = 3 * denoiser.m_wlen15;
    std::vector<short> input(count), output(count + 2048);
    uint32_t random = 1234567;
    for (int block = 0; block < 8000; ++block) {
        for (int i = 0; i < count; ++i) {
            random = random * 1664525u + 1013904223u;
            const double tone = std::sin(2 * 3.141592653589793 * 700 * (block * count + i) / 48000);
            const int phase = block % 1000;
            input[i] = phase < 50 ? 0 : phase < 100 ?
                (i % 48 < 24 ? 32767 : -32768) :
                static_cast<short>((phase < 500 ? 12000 : 30000) * tone + (int)(random >> 20) - 2048);
        }
        int written = -1;
        assert(denoiser.Data_procese(input.data(), output.data(), count, written) == 0);
        assert(written > 0 && written <= (int)output.size());
    }
    std::cout << "Audio NR: 8000 real denoiser blocks (12.8 minutes of audio) passed.\n";
}
