#pragma once
#include <rtl-sdr.h>
#include <config.h>
#include <gui/smgui.h>
#include <algorithm>
#include <array>
#include <string>

// Private source-module state; no exported SDR++ object layouts change.
class R82xxControls {
public:
    bool manual = false;
    bool supported = false;
    bool bypassed = false;
    int lna = 0, mixer = 0, vga = 8, bandwidth = 0, error = 0;
    std::array<int, 3> readback{-1, -1, -1};

    void load(const json& device, int aggregateGain, enum rtlsdr_tuner type) {
#ifdef RTLSDR_BEIGE_GAIN_STAGES
        supported = type == RTLSDR_TUNER_R820T || type == RTLSDR_TUNER_R828D;
#else
        supported = false;
#endif
        manual = false;
        saved = false;
        lna = mixer = bandwidth = error = 0;
        vga = 8;
        readback = {-1, -1, -1};
        const auto defaults = defaultStages(aggregateGain);
        lna = defaults[0];
        mixer = defaults[1];
        if (!device.contains("manualTuner") || !device["manualTuner"].is_object()) return;
        const auto& pref = device["manualTuner"];
        saved = pref.contains("saved") && pref["saved"].is_boolean() && pref["saved"].get<bool>();
        manual = supported && pref.contains("enabled") && pref["enabled"].is_boolean() && pref["enabled"].get<bool>();
        lna = integer(pref, "lna", lna, 15);
        mixer = integer(pref, "mixer", mixer, 15);
        vga = integer(pref, "vga", vga, 15);
        const int bw = integer(pref, "bandwidth", 0, 2430000);
        if (std::find(bandwidths.begin(), bandwidths.end(), bw) != bandwidths.end()) bandwidth = bw;
    }

    json preferences() const {
        return {{"enabled", manual}, {"saved", saved}, {"lna", lna},
            {"mixer", mixer}, {"vga", vga}, {"bandwidth", bandwidth}};
    }

    json state() const {
        auto result = preferences();
        result["supported"] = supported;
        result["bypassed"] = bypassed;
        result["error"] = error;
        result["active_bandwidth"] = manual && !bypassed ? bandwidth : 0;
        result["readback"] = readback;
        result["readback_kind"] = "Programmed manual stages; RF indicator for AGC stages";
        return result;
    }

    bool setMode(bool next, rtlsdr_dev_t* dev) {
        if (next == manual) return false;
        if (next && (!supported || bypassed)) return false;
#ifdef RTLSDR_BEIGE_GAIN_STAGES
        if (next && !saved && dev) {
            error = rtlsdr_beige_get_gain_stages(dev, &lna, &mixer, &vga);
            if (error < 0) return false;
        }
#endif
        manual = next;
        if (next) saved = true;
        error = 0;
        return true;
    }

    int apply(rtlsdr_dev_t* dev, bool applyBandwidth) {
        if (!manual || !supported || bypassed || !dev) return 0;
#ifdef RTLSDR_BEIGE_GAIN_STAGES
        error = applyBandwidth ? rtlsdr_set_tuner_bandwidth(dev, bandwidth) : 0;
        if (error >= 0) error = rtlsdr_beige_set_gain_stages(dev, lna, mixer, vga);
#endif
        return error;
    }

    void capture(rtlsdr_dev_t* dev) {
#ifdef RTLSDR_BEIGE_GAIN_STAGES
        if (dev && supported && !bypassed)
            error = rtlsdr_beige_get_gain_stages(dev, &readback[0], &readback[1], &readback[2]);
#endif
    }

    bool resetDefaults(int aggregateGain) {
        if (!manual || !supported || bypassed) return false;
        const auto defaults = defaultStages(aggregateGain);
        lna = defaults[0];
        mixer = defaults[1];
        vga = defaults[2];
        bandwidth = error = 0;
        saved = true;
        readback = {-1, -1, -1};
        return true;
    }

    bool drawMode(const std::string& name, rtlsdr_dev_t* dev) {
        ImGui::PushID(name.c_str());
        bool changed = false;
        SmGui::ForceSync();
        if (SmGui::RadioButton("Auto##rtl_tuner_mode", !manual)) changed = setMode(false, dev);
        tooltip("Standard driver gain distribution and automatic hardware bandwidth.\nTuner AGC remains your separate choice.");
        SmGui::SameLine();
        if (!supported || bypassed) SmGui::BeginDisabled();
        if (SmGui::RadioButton("Manual##rtl_tuner_mode", manual)) changed = setMode(true, dev);
        tooltip(bypassed ? "The tuner is bypassed in direct sampling." :
            "Individual LNA, mixer and VGA gain, plus hardware bandwidth.\nUseful for balancing sensitivity against overload.");
        if (!supported || bypassed) SmGui::EndDisabled();
        if (error < 0) SmGui::Text(("Tuner setting failed: " + std::to_string(error)).c_str());
        ImGui::PopID();
        return changed;
    }

    bool drawManual(const std::string& name, int aggregateGain, bool& bandwidthChanged) {
        if (!manual || !supported || bypassed) return false;
        ImGui::PushID(name.c_str());
        bool changed = false;
        const std::array<const char*, 3> names{"LNA step", "Mixer step", "VGA step"};
        const std::array<int*, 3> values{&lna, &mixer, &vga};
        for (int i = 0; i < 3; ++i) {
            SmGui::LeftLabel(names[i]);
            SmGui::FillWidth();
            SmGui::ForceSync();
            changed |= SmGui::SliderInt(("##rtl_stage_"+std::to_string(i)).c_str(), values[i], 0, 15, SmGui::FMT_STR_INT_DEFAULT);
            tooltip(i == 2 ? "Hardware VGA gain step 0-15, not calibrated dB.\nAdjusts level into the ADC; excessive gain can clip.\nThis does not enable analog IF AGC." :
                "Hardware gain step 0-15, not calibrated dB.\nMore early-stage gain improves sensitivity but can increase overload.");
        }
        int index = std::find(bandwidths.begin(), bandwidths.end(), bandwidth) - bandwidths.begin();
        SmGui::LeftLabel("Tuner BW");
        SmGui::FillWidth();
        if (SmGui::Combo("##rtl_tuner_bw", &index,
                "Auto\0~350 kHz\0~450 kHz\0~700 kHz\0~900 kHz\0~1.2 MHz\0~1.7 MHz\0~2.05 MHz\0~2.43 MHz\0")) {
            bandwidth = bandwidths[index];
            bandwidthChanged = changed = true;
        }
        tooltip("Analog IF bandwidth for the whole capture, before digitization.\nNarrow settings attenuate off-center VFOs. Values are nominal.\nAuto follows the hardware sample rate, not display decimation.");
        SmGui::FillWidth();
        if (SmGui::Button("Reset to defaults##rtl_manual_defaults")) {
            resetDefaults(aggregateGain);
            bandwidthChanged = changed = true;
        }
        tooltip("Restore the standard LNA/mixer distribution for your saved Gain setting,\nVGA step 8 and automatic bandwidth. Keeps Manual selected.");
        ImGui::PopID();
        return changed;
    }

    bool update(const json& request, rtlsdr_dev_t* dev) {
        if (!request.is_object() || !supported || bypassed) return false;
        if (request.contains("enabled") && !request["enabled"].is_boolean()) return false;
        const bool next = request.contains("enabled") ? request["enabled"].get<bool>() : manual;
        for (const char* key : {"lna", "mixer", "vga"})
            if (request.contains(key) && (!request[key].is_number_integer() || request[key] < 0 || request[key] > 15)) return false;
        if (request.contains("bandwidth") && (!request["bandwidth"].is_number_integer() || request["bandwidth"] < 0 || request["bandwidth"] > 2430000 ||
                std::find(bandwidths.begin(), bandwidths.end(), request["bandwidth"].get<int>()) == bandwidths.end())) return false;
        if (!next && (request.contains("lna") || request.contains("mixer") || request.contains("vga") || request.contains("bandwidth"))) return false;
        if (!setMode(next, dev) && next != manual) return false;
        if (request.contains("lna")) lna = request["lna"].get<int>();
        if (request.contains("mixer")) mixer = request["mixer"].get<int>();
        if (request.contains("vga")) vga = request["vga"].get<int>();
        if (request.contains("bandwidth")) bandwidth = request["bandwidth"].get<int>();
        return true;
    }

private:
    bool saved = false;
    static constexpr std::array<int, 9> bandwidths{0, 350000, 450000, 700000, 900000, 1200000, 1700000, 2050000, 2430000};
    static constexpr std::array<int, 16> lnaSteps{0, 9, 13, 40, 38, 13, 31, 22, 26, 31, 26, 14, 19, 5, 35, 13};
    static constexpr std::array<int, 16> mixerSteps{0, 5, 10, 10, 19, 9, 10, 25, 17, 10, 8, 16, 13, 6, 3, -8};
    static std::array<int, 3> defaultStages(int aggregateGain) {
        int total = 0, lna = 0, mixer = 0;
        for (int i = 0; i < 15 && total < aggregateGain; ++i) {
            total += lnaSteps[++lna];
            if (total < aggregateGain) total += mixerSteps[++mixer];
        }
        return {lna, mixer, 8};
    }
    static int integer(const json& value, const char* key, int fallback, int maximum) {
        if (!value.contains(key) || !value[key].is_number_integer()) return fallback;
        if (value[key] < 0) return 0;
        if (value[key] > maximum) return maximum;
        return value[key].get<int>();
    }
    static void tooltip(const char* text) {
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 26);
            ImGui::TextUnformatted(text);
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
    }
};
