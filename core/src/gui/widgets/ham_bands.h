#pragma once

#include <array>
#include <cstdint>

namespace beige {

struct HamBand {
    const char* label;
    int64_t lowerHz;
    int64_t upperHz;
    uint32_t color;
    const char* bestTime;
    const char* typicalUse;
    const char* typicalDistance;
    constexpr int64_t centerHz() const { return lowerHz + (upperHz - lowerHz) / 2; }
    bool contains(double frequency) const { return frequency >= lowerHz && frequency <= upperHz; }
};

// Receive-navigation ranges from Oz's supplied band chart, not transmit limits.
inline constexpr std::array<HamBand, 16> hamBands{{
    {"160m",    1800000,    2000000, 0xAC2932,
        "Night for DX; ground-wave contacts locally.",
        "DX, contesting, local contacts and emergency communications.",
        "Regional: about 100-1,000 km.\nNight DX: about 1,000-3,000 km."},
    {"80m",     3500000,    4000000, 0xAF481D,
        "Night, especially for DX.",
        "DX, nets and emergency communications.",
        "Regional: about 100-1,500 km.\nNight DX: about 1,500-5,000 km."},
    {"60m",     5330000,    5405000, 0xCE891E,
        "Daytime for regional contacts.",
        "Regional contacts and mid-latitude DX.",
        "Regional: about 100-1,000 km.\nNight DX: about 1,000-3,000 km."},
    {"40m",     7000000,    7300000, 0xDEB52D,
        "Day and night. Regional by day; DX at night.",
        "Local and regional contacts, nets, DX and contests.",
        "Regional: about 300-1,500 km.\nDX: about 1,500-7,000 km."},
    {"30m",    10100000,   10150000, 0x527E29,
        "Daytime for regional contacts; DX can continue after dark.",
        "DX, CW, narrow-band digital and maritime-mobile contacts.",
        "Routine DX: about 1,000-8,000 km."},
    {"20m",    14000000,   14350000, 0x247344,
        "Daytime, especially with higher solar activity.",
        "DX, contests, field operation and general contacts.",
        "Routine DX: about 2,000-10,000 km."},
    {"17m",    18068000,   18160000, 0x275CA0,
        "Daytime; best when the band is open.",
        "DX during favorable band openings.",
        "Open-band DX: about 2,000-10,000 km."},
    {"15m",    21000000,   21450000, 0x246F9C,
        "Daytime; strongly dependent on solar activity.",
        "DX and long-distance contacts during openings.",
        "Open-band DX: about 2,000-10,000 km."},
    {"12m",    24890000,   24990000, 0x563C8E,
        "Daytime; best when the band is open.",
        "DX during favorable band openings.",
        "Open-band DX: about 2,000-10,000 km."},
    {"10m",    28000000,   29700000, 0xA22C58,
        "Daytime with favorable solar activity; other openings vary.",
        "DX, contests and worldwide contacts during openings.",
        "Local: about 20-100 km.\nOpen-band DX: about 1,500-10,000 km."},
    {"6m",     50000000,   54000000, 0x37775C,
        "Any time locally; DX during band openings.",
        "Local and regional contacts, plus DX when propagation permits.",
        "Local: about 30-150 km.\nSporadic E: about 800-2,000 km."},
    {"2m",    144000000,  148000000, 0x345F89,
        "Any time for local and repeater contacts.",
        "Local contacts, repeaters, FM and satellites.",
        "FM / repeaters: about 10-100 km.\nSSB / CW: about 100-400 km."},
    {"1.25m", 222000000,  225000000, 0x71589B,
        "Any time for local and repeater contacts.",
        "Local contacts and repeaters, mainly FM.",
        "FM / repeaters: about 10-80 km.\nWeak signal: about 100-300 km."},
    {"70cm",  420000000,  450000000, 0x984365,
        "Any time for local and repeater contacts.",
        "Local contacts, repeaters, FM and digital modes.",
        "FM / repeaters: about 10-80 km.\nSSB / CW: about 50-300 km."},
    {"33cm",  902000000,  928000000, 0xA95069,
        "Any time for local contacts.",
        "Local contacts, repeaters, digital modes and microwave work.",
        "Local FM: about 10-50 km.\nDirectional antennas: about 50-200 km."},
    {"23cm", 1240000000, 1300000000, 0x75558F,
        "Any time locally; satellites follow pass times.",
        "Microwave work, satellites and Earth-Moon-Earth contacts.",
        "Local FM: about 10-50 km.\nTerrestrial weak signal: about 50-300 km."},
}};

} // namespace beige
