#pragma once
#include <radio_interface.h>
#include <vector>

namespace dsp::baseband {
enum class Profile { Automatic, Original, Digital };
enum class Algorithm { Original, Voice, Bypass };

inline bool ssb(int mode) { return mode == RADIO_IFACE_MODE_USB || mode == RADIO_IFACE_MODE_LSB; }
inline bool protectedMode(int mode) {
    return mode == RADIO_IFACE_MODE_RAW || mode == RADIO_IFACE_MODE_NFM || mode == RADIO_IFACE_MODE_WFM;
}

// Baseband is shared by every VFO. A selected voice receiver must not override
// another receiver's FM/raw protection or apply speech gain to AM/CW.
inline Algorithm select(Profile profile, int selected, const std::vector<int>& active) {
    if (profile == Profile::Digital) return Algorithm::Bypass;
    if (profile == Profile::Original) return Algorithm::Original;
    if (protectedMode(selected)) return Algorithm::Bypass;
    bool voiceOnly = ssb(selected);
    for (int mode : active) {
        if (protectedMode(mode)) return Algorithm::Bypass;
        voiceOnly = voiceOnly && ssb(mode);
    }
    return voiceOnly ? Algorithm::Voice : Algorithm::Original;
}

inline const char* label(Algorithm algorithm) {
    switch (algorithm) {
        case Algorithm::Voice: return "Wiener voice";
        case Algorithm::Bypass: return "Bypassed";
        default: return "Original LogMMSE";
    }
}
inline const char* modeLabel(int mode) {
    switch (mode) {
        case RADIO_IFACE_MODE_NFM: return "NFM";
        case RADIO_IFACE_MODE_WFM: return "WFM";
        case RADIO_IFACE_MODE_AM: return "AM";
        case RADIO_IFACE_MODE_DSB: return "DSB";
        case RADIO_IFACE_MODE_USB: return "USB";
        case RADIO_IFACE_MODE_CW: return "CW";
        case RADIO_IFACE_MODE_LSB: return "LSB";
        case RADIO_IFACE_MODE_RAW: return "RAW";
        default: return "No selected radio";
    }
}
}
