#pragma once

#include "ham_bands.h"
#include <cstddef>
#include <string_view>

namespace beige {

enum class BandCategory { Ham, Broadcast, Utility };
inline constexpr std::array<BandCategory, 3> bandCategories{
    BandCategory::Ham, BandCategory::Broadcast, BandCategory::Utility};

constexpr const char* categoryLabel(BandCategory category) {
    switch (category) {
        case BandCategory::Broadcast: return "Broadcast";
        case BandCategory::Utility: return "Utility";
        default: return "HAM";
    }
}

inline bool parseCategory(std::string_view label, BandCategory& result) {
    for (const auto category : bandCategories) {
        if (label == categoryLabel(category)) { result = category; return true; }
    }
    return false;
}

// Receive search windows; references and regional/gap notes: beige/BAND-PRESETS.md.
// Copper -> amber for broadcast, steel blue -> teal for utility. HAM stays unchanged.
constexpr uint32_t bandGradient(uint32_t from, uint32_t to, unsigned index, unsigned last) {
    uint32_t result = 0;
    for (unsigned shift : {0u, 8u, 16u}) {
        const int a = (from >> shift) & 255, b = (to >> shift) & 255;
        result |= uint32_t(a + (b - a) * int(index) / int(last)) << shift;
    }
    return result;
}
constexpr uint32_t broadcastColor(unsigned i) { return bandGradient(0x92512D, 0xC39141, i, 16); }
constexpr uint32_t utilityColor(unsigned i) { return bandGradient(0x285877, 0x287B72, i, 15); }

inline constexpr const char* nightCoverage = "Regional to long-distance skywave; strongly dependent on propagation.";
inline constexpr const char* dayCoverage = "Long-distance skywave when open; solar activity and path matter.";
inline constexpr const char* lowHfTime = "Evening and night; lower bands usually favor darkness.";
inline constexpr const char* highHfTime = "Daytime; higher bands need favorable propagation.";

inline constexpr std::array<HamBand, 17> broadcastBands{{
    {"LW", 148500, 283500, broadcastColor(0), "Any time locally; distant stations after dark.",
        "Long-wave AM broadcasting (Region 1).", "Ground wave regionally; longer skywave reception at night."},
    {"MW", 526500, 1606500, broadcastColor(1), "Daytime locally; evening and night for distant stations.",
        "Medium-wave AM broadcasting (Region 1).", "Local and regional by day; distant skywave after dark."},
    {"120m", 2300000, 2495000, broadcastColor(2), lowHfTime,
        "Tropical shortwave broadcasting; activity can be sparse.", nightCoverage},
    {"90m", 3200000, 3400000, broadcastColor(3), lowHfTime,
        "Tropical shortwave broadcasting; activity can be sparse.", nightCoverage},
    {"75m", 3950000, 4000000, broadcastColor(4), lowHfTime,
        "Shortwave broadcasting in Regions 1 and 3.", nightCoverage},
    {"60m", 4750000, 5060000, broadcastColor(5), lowHfTime,
        "Tropical shortwave search window. 4.995-5.005 MHz is a time-signal gap.", nightCoverage},
    {"49m", 5900000, 6200000, broadcastColor(6), lowHfTime,
        "International shortwave broadcasting.", nightCoverage},
    {"41m", 7200000, 7450000, broadcastColor(7), "Evening and night; some daytime reception.",
        "International shortwave broadcasting; Region 1 search range.", nightCoverage},
    {"31m", 9400000, 9900000, broadcastColor(8), "Day and night; often useful around sunrise and sunset.",
        "International shortwave broadcasting.", nightCoverage},
    {"25m", 11600000, 12100000, broadcastColor(9), "Daytime and early evening; openings vary.",
        "International shortwave broadcasting.", dayCoverage},
    {"22m", 13570000, 13870000, broadcastColor(10), highHfTime,
        "International shortwave broadcasting.", dayCoverage},
    {"19m", 15100000, 15800000, broadcastColor(11), highHfTime,
        "International shortwave broadcasting.", dayCoverage},
    {"16m", 17480000, 17900000, broadcastColor(12), highHfTime,
        "International shortwave broadcasting.", dayCoverage},
    {"15m", 18900000, 19020000, broadcastColor(13), highHfTime,
        "International shortwave broadcasting; activity can be sparse.", dayCoverage},
    {"13m", 21450000, 21850000, broadcastColor(14), highHfTime,
        "International shortwave broadcasting.", dayCoverage},
    {"11m", 25670000, 26100000, broadcastColor(15), highHfTime,
        "Shortwave broadcasting; usually sparse outside strong openings.", dayCoverage},
    {"FM", 87500000, 108000000, broadcastColor(16), "Any time within station coverage.",
        "VHF FM broadcasting; select WFM in Radio.", "Usually local or regional; terrain and antenna height matter."},
}};

inline constexpr std::array<HamBand, 16> utilityBands{{
    {"Air 3", 2850000, 3025000, utilityColor(0), lowHfTime,
        "HF aeronautical route communications, usually USB voice or data.", nightCoverage},
    {"Sea 4", 4000000, 4063000, utilityColor(1), lowHfTime,
        "Maritime HF simplex search window; usually USB voice.", nightCoverage},
    {"Air 5", 5480000, 5680000, utilityColor(2), lowHfTime,
        "HF aeronautical route communications, usually USB voice or data.", nightCoverage},
    {"Air 6", 6525000, 6685000, utilityColor(3), "Day and night, depending on the path.",
        "HF aeronautical route communications, usually USB voice or data.", nightCoverage},
    {"Sea 8", 8100000, 8195000, utilityColor(4), "Day and evening; propagation varies by path.",
        "Maritime HF simplex search window; usually USB voice.", nightCoverage},
    {"Air 8", 8815000, 8965000, utilityColor(5), "Day and night, depending on the path.",
        "HF aeronautical route communications, usually USB voice or data.", nightCoverage},
    {"Gov HF", 11175000, 11275000, utilityColor(6), "Day and evening; listen when the path is open.",
        "Off-route aeronautical HF search window, including government/military aviation. Not an exclusive government band; usually USB or data.", dayCoverage},
    {"Air 11", 11275000, 11400000, utilityColor(7), "Day and evening; listen when the path is open.",
        "HF aeronautical route communications, usually USB voice or data.", dayCoverage},
    {"Air 13", 13260000, 13360000, utilityColor(8), highHfTime,
        "HF aeronautical route communications, usually USB voice or data.", dayCoverage},
    {"Air 18", 17900000, 17970000, utilityColor(9), highHfTime,
        "HF aeronautical route communications, usually USB voice or data.", dayCoverage},
    {"CB", 26960000, 27410000, utilityColor(10), "Any time locally; daytime DX during openings.",
        "Citizens band voice; AM, FM or SSB depending on the channel.", "Local ground wave; long-distance skywave during openings."},
    {"Nav", 108000000, 117975000, utilityColor(11), "Any time within ground-station coverage.",
        "VOR, ILS localizer and other aeronautical navigation/data signals.", "Line of sight; many signals are navigation tones or data, not speech."},
    {"Air VHF", 117975000, 137000000, utilityColor(12), "Any time aircraft and ground stations are active.",
        "Civil air traffic voice (AM) and aeronautical data.", "Line of sight; aircraft altitude can greatly extend reception."},
    {"Marine", 156025000, 162025000, utilityColor(13), "Any time near active ports and shipping routes.",
        "Maritime VHF voice (NFM), DSC and AIS. Includes gaps and duplex channels.", "Mostly local coastal and ship-to-ship coverage; antenna height matters."},
    {"Mil Air", 225000000, 400000000, utilityColor(14), "During local flight activity.",
        "UHF military aviation search window, often AM. Includes navigation, satellite and other services.", "Mostly line of sight for aircraft voice; not every frequency carries speech."},
    {"PMR", 446000000, 446200000, utilityColor(15), "Any time nearby users are active.",
        "PMR446 local handheld voice, analogue NFM or digital.", "Short-range local reception; buildings and terrain dominate."},
}};

struct BandList {
    const HamBand* data;
    std::size_t size;
    constexpr const HamBand* begin() const { return data; }
    constexpr const HamBand* end() const { return data + size; }
    constexpr const HamBand& operator[](std::size_t i) const { return data[i]; }
};

inline constexpr std::size_t maxBandCount = 17;
constexpr BandList bandsFor(BandCategory category) {
    switch (category) {
        case BandCategory::Broadcast: return {broadcastBands.data(), broadcastBands.size()};
        case BandCategory::Utility: return {utilityBands.data(), utilityBands.size()};
        default: return {hamBands.data(), hamBands.size()};
    }
}

// Minimum width for two contiguous rows, without assuming an even band count.
inline float twoRowWidth(const float* widths, std::size_t count, float gap) {
    if (!count) { return 0; }
    float total = 0, left = 0, best = 0;
    for (std::size_t i = 0; i < count; ++i) { total += widths[i]; }
    best = total + (count - 1) * gap;
    for (std::size_t split = 1; split < count; ++split) {
        left += widths[split - 1];
        const float a = left + (split - 1) * gap;
        const float b = total - left + (count - split - 1) * gap;
        const float candidate = a > b ? a : b;
        if (candidate < best) { best = candidate; }
    }
    return best;
}

} // namespace beige
