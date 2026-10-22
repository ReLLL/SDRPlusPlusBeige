#include "omlsa_mcra/Datablock_Read.h"
#include <utils/flog.h>
#include <cassert>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <new>
#include <vector>

char* sdrppResourcesDirectory = nullptr;
namespace flog {
std::string formatString(const char* message, const std::vector<std::string>&) { return message; }
void __log__(Type, const char* message, const std::vector<std::string>&) { std::cerr << message << '\n'; }
}

static size_t outstandingArrayBytes = 0;
struct alignas(std::max_align_t) ArrayHeader { size_t bytes; };
void* operator new[](size_t size) {
    auto* header = static_cast<ArrayHeader*>(std::malloc(size + sizeof(ArrayHeader)));
    if (!header) throw std::bad_alloc();
    header->bytes = size;
    outstandingArrayBytes += size;
    return header + 1;
}
void operator delete[](void* pointer) noexcept {
    if (!pointer) return;
    auto* header = static_cast<ArrayHeader*>(pointer) - 1;
    outstandingArrayBytes -= header->bytes;
    std::free(header);
}
void operator delete[](void* pointer, size_t) noexcept { operator delete[](pointer); }

int main(int argc, char** argv) {
    assert(argc == 2);
    sdrppResourcesDirectory = argv[1];
    const size_t before = outstandingArrayBytes;
    for (int reset = 0; reset < 32; ++reset) {
        {
            Datablock_Read denoiser(48000, 1, 10000);
            assert(denoiser.m_derr_code == 0);
            const int count = 3 * denoiser.m_wlen15;
            std::vector<short> input(count, 0), output(count + 2048);
            int written = -1;
            assert(denoiser.Data_procese(input.data(), output.data(), count, written) == 0);
        }
        std::cout << "Outstanding NR array bytes after reset " << reset << ": " << outstandingArrayBytes - before << '\n';
        assert(outstandingArrayBytes == before);
    }
}
