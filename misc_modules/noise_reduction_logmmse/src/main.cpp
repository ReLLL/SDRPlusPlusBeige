#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif

#include <imgui.h>
#include <module.h>
#include <gui/gui.h>
#include <config.h>
#include <core.h>
#include <imgui/imgui_internal.h>
#include <vector>
#include <tuple>
#include <gui/widgets/snr_meter.h>
#include "signal_path/signal_path.h"
#include <radio_interface.h>
#include "if_nr.h"
#include "af_nr.h"
#include "baseband_mode.h"
#ifdef BEIGE_NEURAL_AUDIO
#include "neural_audio.h"
#include "neural_model_path.h"
#include "neural_clarity.h"
#endif
#include <http_debug_server.h>

using namespace ImGui;

ConfigManager config;


SDRPP_MOD_INFO{
    /* Name:            */ "noise_reduction_logmmse",
    /* Description:     */ "LOGMMSE noise reduction",
    /* Author:          */ "sannysanoff",
    /* Version:         */ 0, 1, 0,
    /* Max instances    */ -1
};

class NRModule : public ModuleManager::Instance {

    dsp::IFNRLogMMSE ifnrProcessor;

    std::unordered_map<std::string, std::shared_ptr<dsp::AFNRLogMMSE>> afnrProcessors;      // instance by radio name.
    std::unordered_map<std::string, std::shared_ptr<dsp::AFNR_OMLSA_MCRA>> afnrProcessors2; // instance by radio name.
#ifdef BEIGE_NEURAL_AUDIO
    std::unordered_map<std::string, std::shared_ptr<dsp::NeuralVoiceNR>> neuralProcessors;
#endif

public:
    NRModule(std::string name) {
        this->name = name;
        config.acquire();
        if (config.conf.contains("IFNR")) ifnr = config.conf["IFNR"];
        if (config.conf.contains("DisableCpuDeactivation")) disableCpuDeactivation = config.conf["DisableCpuDeactivation"];
        if (config.conf.contains("SNRChartWidget")) snrChartWidget = config.conf["SNRChartWidget"];
        if (config.conf.contains("BasebandAcceleration")) acceleration = config.conf["BasebandAcceleration"];
        if (config.conf.contains("BasebandProfile") && config.conf["BasebandProfile"].is_number_integer()) {
            const auto& saved = config.conf["BasebandProfile"];
            if (saved >= 0 && saved <= 2) profile = saved.get<int>();
        }
        config.release(true);

        ifnrProcessor.setDisableCpuDeactivation(disableCpuDeactivation);
        ifnrProcessor.setAcceleration(acceleration);

        gui::menu.registerEntry(name, menuHandler, this, NULL);
        updateBindings();
        refreshBasebandMode();

        std::string path = "/modules/noise_reduction_logmmse/" + name;
        httpdebug::procfs::registerEndpoint(path + "/baseband_nr", [&]() { return ifnr ? "true" : "false"; }, [&](const std::string& v) { ifnr = (v == "true" || v == "1"); refreshBasebandMode(); }, httpdebug::procfs::Type::Bool);
        httpdebug::procfs::registerEndpoint(path + "/snr_chart", [&]() { return snrChartWidget ? "true" : "false"; }, [&](const std::string& v) { snrChartWidget = (v == "true" || v == "1"); }, httpdebug::procfs::Type::Bool);
        httpdebug::procfs::registerEndpoint(path + "/cpu_usage", [&]() { return std::to_string(ifnrProcessor.percentUsage.load()); }, nullptr, httpdebug::procfs::Type::Int);
        httpdebug::procfs::registerEndpoint(path + "/acceleration", [&]() { return acceleration ? "true" : "false"; }, [&](const std::string& v) {
            acceleration = (v == "true" || v == "1");
            ifnrProcessor.setAcceleration(acceleration);
        }, httpdebug::procfs::Type::Bool);
        httpdebug::procfs::registerEndpoint(path + "/backend", [&]() { return ifnrProcessor.usesApple() ? "Apple Accelerate" : "FFTW"; }, nullptr, httpdebug::procfs::Type::String);
        httpdebug::procfs::registerEndpoint(path + "/profile", [&]() { return std::string(profileName()); }, [&](const std::string& v) {
            if (v == "auto") setProfile(0);
            else if (v == "original") setProfile(1);
            else if (v == "digital") setProfile(2);
        }, httpdebug::procfs::Type::String);
        httpdebug::procfs::registerEndpoint(path + "/effective", [&]() { return dsp::baseband::label(effective.load()); }, nullptr, httpdebug::procfs::Type::String);
        httpdebug::procfs::registerEndpoint(path + "/selected_mode", [&]() { return dsp::baseband::modeLabel(selectedMode.load()); }, nullptr, httpdebug::procfs::Type::String);
    }

    ~NRModule() {
        std::string path = "/modules/noise_reduction_logmmse/" + name;
        httpdebug::procfs::unregister(path + "/baseband_nr");
        httpdebug::procfs::unregister(path + "/snr_chart");
        httpdebug::procfs::unregister(path + "/cpu_usage");
        httpdebug::procfs::unregister(path + "/acceleration");
        httpdebug::procfs::unregister(path + "/backend");
        httpdebug::procfs::unregister(path + "/profile");
        httpdebug::procfs::unregister(path + "/effective");
        httpdebug::procfs::unregister(path + "/selected_mode");
        if (enabled) { enabled = false; updateBindings(); }
        gui::menu.removeEntry(name);
    }

    void postInit() { refreshBasebandMode(); }

    void enable() {
        if (!enabled) {
            enabled = true;
            ifnrProcessor.setDisableCpuDeactivation(disableCpuDeactivation);
            updateBindings();
            refreshBasebandMode();
        }
    }

    void disable() {
        if (enabled) {
            enabled = false;
            actuateIFNR();
            updateBindings();
        }
    }

    bool isEnabled() {
        return enabled;
    }


private:
    bool ifnr = false;
    bool disableCpuDeactivation = false;
    bool acceleration = true;
    std::atomic<int> profile{0}, selectedMode{-1};
    std::atomic<dsp::baseband::Algorithm> effective{dsp::baseband::Algorithm::Original};

    const char* profileName() const {
        switch (profile.load()) {
            case 1: return "original";
            case 2: return "digital";
            default: return "auto";
        }
    }

    void setProfile(int value) {
        profile = value;
        config.acquire();
        config.conf["BasebandProfile"] = value;
        config.release(true);
        refreshBasebandMode();
    }

    void refreshBasebandMode() {
        std::vector<int> modes;
        int selected = -1;
        for (const auto& radio : core::modComManager.findInterfaces("radio")) {
            if (!sigpath::vfoManager.vfoExists(radio)) continue;
            int mode = -1;
            core::modComManager.callInterface(radio, RADIO_IFACE_CMD_GET_MODE, nullptr, &mode);
#ifdef BEIGE_NEURAL_AUDIO
            auto neural = neuralProcessors.find(radio);
            if (neural != neuralProcessors.end()) {
                neural->second->voiceMode = mode == RADIO_IFACE_MODE_USB || mode == RADIO_IFACE_MODE_LSB
                    || mode == RADIO_IFACE_MODE_AM || mode == RADIO_IFACE_MODE_DSB || mode == RADIO_IFACE_MODE_NFM;
                neural->second->sampleRate = int(sigpath::sinkManager.getStreamSampleRate(radio));
            }
#endif
            modes.push_back(mode);
            if (radio == gui::waterfall.selectedVFO) selected = mode;
        }
        selectedMode = selected;
        const auto algorithm = dsp::baseband::select(static_cast<dsp::baseband::Profile>(profile.load()), selected, modes);
        effective = algorithm;
        ifnrProcessor.setVoice(algorithm == dsp::baseband::Algorithm::Voice);
        actuateIFNR();
    }

    bool afnrEnabled = false;
    bool snrChartWidget = false;

    void attachAFToRadio(const std::string& instanceName) {
        auto afnrlogmmse = std::make_shared<dsp::AFNRLogMMSE>();
        afnrProcessors[instanceName] = afnrlogmmse;
        afnrlogmmse->init(nullptr);
        const std::shared_ptr<dsp::AFNR_OMLSA_MCRA> afnromlsa = std::make_shared<dsp::AFNR_OMLSA_MCRA>();
        afnromlsa->init(nullptr);
        afnrProcessors2[instanceName] = afnromlsa;
        core::modComManager.callInterface(instanceName, RADIO_IFACE_CMD_ADD_TO_IFCHAIN, afnrlogmmse.get(), NULL);
        core::modComManager.callInterface(instanceName, RADIO_IFACE_CMD_ADD_TO_AFCHAIN, afnromlsa.get(), NULL);
        core::modComManager.callInterface(instanceName, RADIO_IFACE_CMD_ENABLE_IN_AFCHAIN, afnromlsa.get(), NULL);
        config.acquire();

        bool afnr = false;
        // if (config.conf.contains("AF_NR_"+instanceName)) afnr = config.conf["AF_NR_"+instanceName];
        auto frequency = 10;
        if (config.conf.contains("AF_NRF_" + instanceName)) frequency = config.conf["AF_NRF_" + instanceName];

        bool afnr2 = false;
        if (config.conf.contains("AF_NR2_" + instanceName)) afnr2 = config.conf["AF_NR2_" + instanceName];


        config.release(true);
        afnrlogmmse->afnrBandwidth = frequency;
        afnrlogmmse->setProcessingBandwidth(frequency * 1000);
        afnrlogmmse->allowed = afnr;

        afnromlsa->allowed = afnr2;

#ifdef BEIGE_NEURAL_AUDIO
        auto neural = std::make_shared<dsp::NeuralVoiceNR>();
        neural->init(nullptr);
        neural->modelPath = dsp::neuralModelPath(sdrppResourcesDirectory);
        neural->sampleRate = int(sigpath::sinkManager.getStreamSampleRate(instanceName));
        config.acquire();
        neural->allowed = config.conf.value("NeuralVoiceNR_" + instanceName, false);
        neural->clarityOptions = config.conf.value("NeuralClarity_" + instanceName, 0u) & dsp::NeuralClarity::All;
        config.release();
        // Preserve an existing Audio NR2 choice if a saved profile enables both.
        if (afnr2) neural->allowed = false;
        neuralProcessors[instanceName] = neural;
        core::modComManager.callInterface(instanceName, RADIO_IFACE_CMD_ADD_TO_AFCHAIN, neural.get(), nullptr);
        core::modComManager.callInterface(instanceName, neural->allowed ? RADIO_IFACE_CMD_ENABLE_IN_AFCHAIN
            : RADIO_IFACE_CMD_DISABLE_IN_AFCHAIN, neural.get(), nullptr);
#endif
        const std::string endpoint = "/modules/noise_reduction_logmmse/" + name + "/audio/" + instanceName;
        httpdebug::procfs::registerEndpoint(endpoint + "/nr2", [afnromlsa]() { return afnromlsa->allowed ? "true" : "false"; },
            [this, instanceName](const std::string& value) { setAudioNR2(instanceName, value == "true" || value == "1"); }, httpdebug::procfs::Type::Bool);
        httpdebug::procfs::registerEndpoint(endpoint + "/nr2_cpu", [afnromlsa]() { return std::to_string(afnromlsa->cpuMeter.percent.load()); }, nullptr, httpdebug::procfs::Type::String);
        httpdebug::procfs::registerEndpoint(endpoint + "/scaling", [afnromlsa]() { return std::to_string(afnromlsa->displayedScaling.load()); }, nullptr, httpdebug::procfs::Type::String);
#ifdef BEIGE_NEURAL_AUDIO
        httpdebug::procfs::registerEndpoint(endpoint + "/neural", [neural]() { return neural->allowed ? "true" : "false"; },
            [this, instanceName](const std::string& value) { setNeural(instanceName, value == "true" || value == "1"); }, httpdebug::procfs::Type::Bool);
        httpdebug::procfs::registerEndpoint(endpoint + "/neural_cpu", [neural]() { return std::to_string(neural->cpuMeter.percent.load()); }, nullptr, httpdebug::procfs::Type::String);
        for (auto [suffix, flag] : std::initializer_list<std::pair<const char*, unsigned>>{
                {"/detail", dsp::NeuralClarity::Detail}, {"/syllables", dsp::NeuralClarity::Smooth}, {"/voice_lift", dsp::NeuralClarity::Lift}}) {
            httpdebug::procfs::registerEndpoint(endpoint + suffix, [neural, flag]() { return neural->clarityOptions.load() & flag ? "true" : "false"; },
                [this, instanceName, flag](const std::string& v) { setClarity(instanceName, flag, v == "true" || v == "1"); }, httpdebug::procfs::Type::Bool);
        }
        httpdebug::procfs::registerEndpoint(endpoint + "/neural_status", [neural]() {
            return !neural->voiceMode ? "Mode bypass" : neural->status < 0 ? "Unavailable; bypassed" : neural->status == 1 ? "Ready" : "Idle";
        }, nullptr, httpdebug::procfs::Type::String);
#endif

        actuateAFNR();
    }

    void detachAFFromRadio(const std::string& instanceName) {
        const std::string endpoint = "/modules/noise_reduction_logmmse/" + name + "/audio/" + instanceName;
        for (const auto* suffix : {"/nr2", "/nr2_cpu", "/scaling", "/neural", "/neural_cpu", "/neural_status", "/detail", "/syllables", "/voice_lift"}) httpdebug::procfs::unregister(endpoint + suffix);
#ifdef BEIGE_NEURAL_AUDIO
        auto neural = neuralProcessors.find(instanceName);
        if (neural != neuralProcessors.end()) {
            core::modComManager.callInterface(instanceName, RADIO_IFACE_CMD_REMOVE_FROM_AFCHAIN, neural->second.get(), nullptr);
            neuralProcessors.erase(neural);
        }
#endif
        if (afnrProcessors.find(instanceName) != afnrProcessors.end()) {
            core::modComManager.callInterface(instanceName, RADIO_IFACE_CMD_REMOVE_FROM_IFCHAIN,
                                              afnrProcessors[instanceName].get(), NULL);
            afnrProcessors.erase(instanceName);
        }
        if (afnrProcessors2.find(instanceName) != afnrProcessors2.end()) {
            core::modComManager.callInterface(instanceName, RADIO_IFACE_CMD_REMOVE_FROM_AFCHAIN,
                                              afnrProcessors2[instanceName].get(), NULL);
            afnrProcessors2.erase(instanceName);
        }
    }

    void updateBindings() {
        if (enabled) {
            flog::info("Enabling noise reduction things");
            gui::mainWindow.onWaterfallDrawn.bindHandler(&waterfallDrawnHandler);
            waterfallDrawnHandler.ctx = this;
            waterfallDrawnHandler.handler = [](ImGuiContext* gctx, void* ctx) {
                NRModule* _this = (NRModule*)ctx;
                _this->refreshBasebandMode();
                _this->drawSNRMeterAverages(gctx);
            };
            ImGui::onSNRMeterExtPoint.bindHandler(&snrMeterExtPointHandler);
            snrMeterExtPointHandler.ctx = this;
            snrMeterExtPointHandler.handler = [](ImGui::SNRMeterExtPoint extp, void* ctx) {
                NRModule* _this = (NRModule*)ctx;
                if (_this->enabled) {
                    _this->lastsnr.insert(_this->lastsnr.begin(), extp.lastDrawnValue);
                    if (_this->lastsnr.size() > NLASTSNR)
                        _this->lastsnr.resize(NLASTSNR);

                    _this->postSnrLocation = extp.postSnrLocation;
                }
            };

            sigpath::iqFrontEnd.addPreprocessor(&ifnrProcessor, false);

            sigpath::sourceManager.onTuneChanged.bindHandler(&currentFrequencyChangedHandler);
            currentFrequencyChangedHandler.ctx = this;
            currentFrequencyChangedHandler.handler = [](double v, void* ctx) {
                auto _this = (NRModule*)ctx;
                _this->ifnrProcessor.reset(); // reset noise profile
            };

            auto names = core::modComManager.findInterfaces("radio");
            for (auto& name : names) {
                attachAFToRadio(name);
            }
            core::moduleManager.onInstanceCreated.bindHandler(&instanceCreatedHandler);
            instanceCreatedHandler.ctx = this;
            instanceCreatedHandler.handler = [](std::string v, void* ctx) {
                auto _this = (NRModule*)ctx;
                auto modname = core::moduleManager.getInstanceModuleName(v);
                if (modname == "radio") {
                    // radio created after the NR module.
                    _this->attachAFToRadio(v);
                    // on detach: module will remove pointer to AF NR from its chain, shared ptr will remain in the map until it gets replaced by a new one (maybe)
                }
                //
            };
        }
        else {
            sigpath::iqFrontEnd.removePreprocessor(&ifnrProcessor);
            sigpath::sourceManager.onTuneChanged.unbindHandler(&currentFrequencyChangedHandler);
            core::moduleManager.onInstanceCreated.unbindHandler(&instanceCreatedHandler);
            auto names = core::modComManager.findInterfaces("radio");
            for (const auto& radio : names) detachAFFromRadio(radio);
            afnrProcessors.clear();
            afnrProcessors2.clear();
#ifdef BEIGE_NEURAL_AUDIO
            neuralProcessors.clear();
#endif
            ImGui::onSNRMeterExtPoint.unbindHandler(&snrMeterExtPointHandler);
            gui::mainWindow.onWaterfallDrawn.unbindHandler(&waterfallDrawnHandler);
        }
    }


    std::unordered_map<std::string, long long> firstTimeHover;

    bool mustShowTooltip(const std::string& key) {
        if (ImGui::IsItemHovered()) {
            auto what = firstTimeHover[key];
            if (what == 0) {
                firstTimeHover[key] = currentTimeMillis();
                return false;
            }
            else {
                return currentTimeMillis() - what > 1000;
            }
        }
        else {
            firstTimeHover[key] = 0;
            return false;
        }
    }

    void actuateAFNR() {
        for (auto [k, v] : afnrProcessors) {
            core::modComManager.callInterface(k, !v->allowed ? RADIO_IFACE_CMD_DISABLE_IN_IFCHAIN : RADIO_IFACE_CMD_ENABLE_IN_IFCHAIN, v.get(), NULL);
        }
        //        for(auto [k, v] : afnrProcessors2) {
        //            core::modComManager.callInterface(k, !v->allowed ? RADIO_IFACE_CMD_DISABLE_IN_AFCHAIN : RADIO_IFACE_CMD_ENABLE_IN_AFCHAIN, v.get(), NULL);
        //        }
    }

    void actuateIFNR() {
        bool shouldRun = enabled && ifnr && effective.load() != dsp::baseband::Algorithm::Bypass;
        if (ifnrProcessor.bypass != !shouldRun) {
            ifnrProcessor.bypass = !shouldRun;
            sigpath::iqFrontEnd.togglePreprocessor(&ifnrProcessor, shouldRun);
        }
    }

    void setAudioNR2(const std::string& radio, bool value) {
#ifdef BEIGE_NEURAL_AUDIO
        if (value) setNeural(radio, false);
#endif
        afnrProcessors2.at(radio)->allowed = value;
        config.acquire();
        config.conf["AF_NR2_" + radio] = value;
        config.release(true);
    }

#ifdef BEIGE_NEURAL_AUDIO
    void setClarity(const std::string& radio, unsigned flag, bool value) {
        auto neural = neuralProcessors.at(radio);
        const unsigned options = value ? neural->clarityOptions.load() | flag : neural->clarityOptions.load() & ~flag;
        neural->clarityOptions = options;
        config.acquire();
        config.conf["NeuralClarity_" + radio] = options;
        config.release(true);
    }

    void setNeural(const std::string& radio, bool value) {
        auto neural = neuralProcessors.at(radio);
        if (value) afnrProcessors2.at(radio)->allowed = false;
        if (value && neural->status < 0) neural->status = 0;
        neural->allowed = value;
        core::modComManager.callInterface(radio, value ? RADIO_IFACE_CMD_ENABLE_IN_AFCHAIN : RADIO_IFACE_CMD_DISABLE_IN_AFCHAIN,
            neural.get(), nullptr);
        config.acquire();
        config.conf["NeuralVoiceNR_" + radio] = value;
        if (value) config.conf["AF_NR2_" + radio] = false;
        config.release(true);
    }
#endif

    void menuHandler() {
        float menuWidth = ImGui::GetContentRegionAvail().x;
        if (ImGui::Checkbox("Baseband NR##_sdrpp_if_nr", &ifnr)) {
            //            sigpath::signalPath.setWidebandNR(_this->widebandNR);
            config.acquire();
            config.conf["IFNR"] = ifnr;
            config.release(true);
            refreshBasebandMode();
        }
        if (ifnrProcessor.stoppedForCpu() && ifnr) {
            ifnr = false;
            config.acquire();
            config.conf["IFNR"] = ifnr;
            config.release(true);
            actuateIFNR();
        }
        if (ifnrProcessor.stoppedForCpu()) {
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0, 0, 1.0f));
            ImGui::TextUnformatted("Slow CPU. Reduce sample rate.");
            ImGui::PopStyleColor(1);
        }
        else {
            // show cpu usage
            if (ifnrProcessor.percentUsage >= 0) {
                ImGui::SameLine();
                if (ifnrProcessor.percentUsage > 80) {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0, 0, 1.0f));
                }
                std::string cpuText = std::to_string(ifnrProcessor.percentUsage.load()) + "% cpu";
                ImVec2 textSize = ImGui::CalcTextSize(cpuText.c_str());
                bool clicked = ImGui::Selectable(cpuText.c_str(), false, ImGuiSelectableFlags_None, textSize);
                if (clicked) {
                    disableCpuDeactivation = !disableCpuDeactivation;
                    ifnrProcessor.setDisableCpuDeactivation(disableCpuDeactivation);
                    config.acquire();
                    config.conf["DisableCpuDeactivation"] = disableCpuDeactivation;
                    config.release(true);
                }
                if (disableCpuDeactivation) {
                    auto drawList = ImGui::GetWindowDrawList();
                    ImVec2 min = ImGui::GetItemRectMin();
                    ImVec2 max = ImGui::GetItemRectMax();
                    drawList->AddLine(ImVec2(min.x, (min.y + max.y) / 2), ImVec2(max.x, (min.y + max.y) / 2), ImGui::GetColorU32(ImGuiCol_Text), 1.0f);
                }
                if (ifnrProcessor.percentUsage > 80) {
                    ImGui::PopStyleColor(1);
                }
            }
        }

#ifdef __APPLE__
        if (ImGui::Checkbox("Automatic acceleration##baseband", &acceleration)) {
            ifnrProcessor.setAcceleration(acceleration);
            config.acquire();
            config.conf["BasebandAcceleration"] = acceleration;
            config.release(true);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Use Apple Accelerate FFTs for supported lengths.\nAutomatically falls back to FFTW. Disable to use FFTW directly.");
        }
#endif
        int choice = profile.load();
        ImGui::SetNextItemWidth(menuWidth);
        if (ImGui::Combo("##baseband_profile", &choice, "Automatic (mode)\0Original LogMMSE\0Digital / bypass\0")) setProfile(choice);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Automatic: Wiener for USB/LSB voice; original for AM/CW/DSB.\nFM/RAW bypasses NR. Mixed VFOs use the safer shared setting.\nFor digital signals carried in USB/LSB, select Digital / bypass.\nOriginal LogMMSE restores the accepted method.");
        }
        if (ifnr) ImGui::TextDisabled("%s: %s", dsp::baseband::modeLabel(selectedMode.load()), dsp::baseband::label(effective.load()));
        if (ifnr && ifnrProcessor.percentUsage.load() >= 0) {
            ImGui::TextDisabled("Math: %s", ifnrProcessor.usesApple() ? "Apple Accelerate" : "FFTW");
        }

        for (auto [k, v] : afnrProcessors2) {
            bool audioNR2 = v->allowed.load();
            if (ImGui::Checkbox(("Audio NR2 " + k + "##_radio_omlsa_nr_" + k).c_str(), &audioNR2)) setAudioNR2(k, audioNR2);
            ImGui::SameLine();
            ImGui::Text("Scaling: %0.01fx", v->displayedScaling.load());
            if (v->allowed && v->cpuMeter.percent.load() >= 0) {
                ImGui::TextDisabled("%.1f%% cpu", v->cpuMeter.percent.load());
            }
#ifdef BEIGE_NEURAL_AUDIO
            auto neural = neuralProcessors.at(k);
            bool voice = neural->allowed.load();
            if (ImGui::Checkbox(("Neural Voice NR " + k + "##neural_" + k).c_str(), &voice)) setNeural(k, voice);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Local GTCRN speech reduction with automatic voice-level recovery.\nEnabling selects it instead of Audio NR2 for this radio.\nCW, RAW and WFM bypass it. For USB/LSB digital signals, leave it off.\nCPU SIMD and optional Apple FFT; portable FFT fallback.");
            if (voice) {
                if (!neural->voiceMode) ImGui::TextDisabled("Bypassed for this mode");
                else if (neural->status < 0) ImGui::TextDisabled("Unavailable; bypassed");
                else if (neural->cpuMeter.percent.load() >= 0) ImGui::TextDisabled("%.1f%% cpu | %s", neural->cpuMeter.percent.load(),
                    neural->appleFFT ? "CPU SIMD + Apple FFT" : "CPU SIMD + FFTW");
                else ImGui::TextDisabled("Starting voice reduction...");
                ImGui::Indent();
                for (auto [label, flag, tip] : std::initializer_list<std::tuple<const char*, unsigned, const char*>>{
                        {"Speech detail", dsp::NeuralClarity::Detail, "Test: blend a little aligned original audio around detected speech.\nMay preserve weak consonants, with some noise during speech."},
                        {"Syllable smoothing", dsp::NeuralClarity::Smooth, "Test: soften brief drops in the neural spectrum around speech.\nMay reduce choppiness, with short noise tails."},
                        {"Voice lift (+1 dB)", dsp::NeuralClarity::Lift, "Test: a small level boost around detected speech.\nDoes not reconstruct speech removed by the model."}}) {
                    bool checked = neural->clarityOptions.load() & flag;
                    if (ImGui::Checkbox((std::string(label) + "##neural_" + k).c_str(), &checked)) setClarity(k, flag, checked);
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
                }
                ImGui::TextDisabled("All off = current base neural NR");
                ImGui::Unindent();
            }
#endif
        }
        if (ImGui::Checkbox(("SNR Chart##_radio_logmmse_nr_" + name).c_str(), &snrChartWidget)) {
            config.acquire();
            config.conf["SNRChartWidget"] = snrChartWidget;
            config.release(true);
        }
    }

    static void menuHandler(void* ctx) {
        NRModule* _this = (NRModule*)ctx;
        _this->menuHandler();
    }

    static const int NLASTSNR = 1500;
    std::vector<float> lastsnr;


    ImVec2 postSnrLocation;

#pragma clang diagnostic push
#pragma ide diagnostic ignored "UnreachableCode"

    void drawSNRMeterAverages(ImGuiContext* gctx) {

        if (!snrChartWidget || !enabled) {
            return;
        }
        static std::vector<float> r;
        static int counter = 0;
        static const int winsize = 10;
        counter++;
        if (counter % winsize == winsize - 1) {
            r = dsp::math::maxeach(winsize, lastsnr);
        }
        ImGuiWindow* window = gctx->CurrentWindow;
        ImU32 text = ImGui::GetColorU32(ImGuiCol_Text);
        for (int q = 1; q < r.size(); q++) {
            window->DrawList->AddLine(postSnrLocation + ImVec2(0 + r[q - 1], q - 1 + window->Pos.y),
                                      postSnrLocation + ImVec2(0 + r[q], q + window->Pos.y), text);
        }
    }

#pragma clang diagnostic pop


    std::string name;
    bool enabled = true;
    EventHandler<ImGuiContext*> waterfallDrawnHandler;
    EventHandler<ImGui::SNRMeterExtPoint> snrMeterExtPointHandler;
    EventHandler<double> currentFrequencyChangedHandler;
    EventHandler<std::string> instanceCreatedHandler;
};


MOD_EXPORT void _INIT_() {
    config.setPath(std::string(core::getRoot()) + "/noise_reduction_logmmse_config.json");
    config.load(json::object());
    config.enableAutoSave();
}

MOD_EXPORT ModuleManager::Instance* _CREATE_INSTANCE_(std::string name) {
    return new NRModule(name);
}

MOD_EXPORT void _DELETE_INSTANCE_(void* instance) {
    delete (NRModule*)instance;
}

MOD_EXPORT void _END_() {
    config.disableAutoSave();
    config.save();
}
