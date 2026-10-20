#pragma once

#include <array>
#include <cstdint>

namespace beige {

struct HamBand {
    const char* label;
    int64_t lowerHz;
    int64_t upperHz;
    uint32_t color;
    constexpr int64_t centerHz() const { return lowerHz + (upperHz - lowerHz) / 2; }
    bool contains(double frequency) const { return frequency >= lowerHz && frequency <= upperHz; }
};

// Receive-navigation ranges from Oz's supplied band chart, not transmit limits.
inline constexpr std::array<HamBand, 16> hamBands{{
    {"160m",    1800000,    2000000, 0xAC2932},
    {"80m",     3500000,    4000000, 0xAF481D},
    {"60m",     5330000,    5405000, 0xCE891E},
    {"40m",     7000000,    7300000, 0xDEB52D},
    {"30m",    10100000,   10150000, 0x527E29},
    {"20m",    14000000,   14350000, 0x247344},
    {"17m",    18068000,   18160000, 0x275CA0},
    {"15m",    21000000,   21450000, 0x246F9C},
    {"12m",    24890000,   24990000, 0x563C8E},
    {"10m",    28000000,   29700000, 0xA22C58},
    {"6m",     50000000,   54000000, 0x37775C},
    {"2m",    144000000,  148000000, 0x345F89},
    {"1.25m", 222000000,  225000000, 0x71589B},
    {"70cm",  420000000,  450000000, 0x984365},
    {"33cm",  902000000,  928000000, 0xA95069},
    {"23cm", 1240000000, 1300000000, 0x75558F},
}};

} // namespace beige
