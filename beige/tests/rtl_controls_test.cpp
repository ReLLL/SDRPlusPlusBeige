#include "r82xx_controls.h"
#include <cassert>
#include <iostream>

static int calls;
extern "C" int rtlsdr_beige_get_gain_stages(rtlsdr_dev_t*, int* lna, int* mixer, int* vga) {
    ++calls;
    *lna = 6; *mixer = 7; *vga = 8;
    return 0;
}

int main() {
    R82xxControls controls;
    controls.load(json::object(), 496, RTLSDR_TUNER_R828D);
    assert(controls.supported && !controls.manual && controls.bandwidth == 0);
    auto* device = reinterpret_cast<rtlsdr_dev_t*>(1);
    const auto initial = controls.preferences();
    // A malformed update must neither touch USB nor partially change mode.
    assert(!controls.update({{"enabled", true}, {"lna", 16}}, device));
    assert(!controls.update({{"enabled", true}, {"bandwidth", 1ULL << 50}}, device));
    assert(controls.preferences() == initial && calls == 0);
    assert(!controls.update({{"lna", 4}}, device));
    assert(controls.update({{"enabled", true}}, device));
    assert(calls == 1 && controls.lna == 6 && controls.mixer == 7 && controls.vga == 8);
    assert(controls.update({{"lna", 4}, {"vga", 10}, {"bandwidth", 900000}}, device));
    const auto manual = controls.preferences();
    assert(controls.update({{"enabled", false}}, device));
    assert(controls.update({{"enabled", true}}, device));
    assert(calls == 1 && controls.preferences() == manual);
    assert(controls.resetDefaults(496));
    assert(controls.manual && controls.lna == initial["lna"] && controls.mixer == initial["mixer"] && controls.vga == 8 && controls.bandwidth == 0);
    assert(controls.update({{"enabled", false}}, device));
    const auto autoDefaults = controls.preferences();
    assert(!controls.resetDefaults(0) && controls.preferences() == autoDefaults);
    R82xxControls restored;
    restored.load({{"manualTuner", manual}}, 0, RTLSDR_TUNER_R828D);
    assert(restored.preferences() == manual);
    restored.load({{"manualTuner", {{"enabled", 7}, {"saved", "invalid"},
        {"lna", 1ULL << 50}, {"mixer", -9}, {"vga", "bad"}, {"bandwidth", 123}}}}, 0, RTLSDR_TUNER_R828D);
    assert(!restored.manual && restored.lna == 15 && restored.mixer == 0 && restored.vga == 8 && restored.bandwidth == 0);
    restored.load({{"manualTuner", manual}}, 0, RTLSDR_TUNER_E4000);
    assert(!restored.supported && !restored.manual && !restored.update({{"enabled", true}}, device));
    controls.bypassed = true;
    assert(!controls.update({{"vga", 12}}, device));
    assert(!controls.resetDefaults(0));
    assert(calls == 1);
    std::cout << "Auto compatibility, atomic validation, one-shot gain snapshot and preference recovery passed.\n";
}
