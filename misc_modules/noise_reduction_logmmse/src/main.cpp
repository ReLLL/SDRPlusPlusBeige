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
#include <gui/widgets/snr_meter.h>
#include "signal_path/signal_path.h"
#include <radio_interface.h>
#include "if_nr.h"
#include "af_nr.h"
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

public:
    NRModule(std::string name) {
        this->name = name;
        config.acquire();
        if (config.conf.contains("IFNR")) ifnr = config.conf["IFNR"];
        if (config.conf.contains("DisableCpuDeactivation")) disableCpuDeactivation = config.conf["DisableCpuDeactivation"];
        if (config.conf.contains("SNRChartWidget")) snrChartWidget = config.conf["SNRChartWidget"];
        if (config.conf.contains("BasebandReductionDB")) reductionDb = std::clamp(float(config.conf["BasebandReductionDB"]), 0.0f, 24.0f);
        if (config.conf.contains("BasebandAcceleration")) acceleration = config.conf["BasebandAcceleration"];
        config.release(true);

        ifnrProcessor.setDisableCpuDeactivation(disableCpuDeactivation);
        ifnrProcessor.setReduction(reductionDb);
        ifnrProcessor.setAcceleration(acceleration);

        gui::menu.registerEntry(name, menuHandler, this, NULL);
        updateBindings();
        actuateIFNR();

        std::string path = "/modules/noise_reduction_logmmse/" + name;
        httpdebug::procfs::registerEndpoint(path + "/baseband_nr", [&]() { return ifnr ? "true" : "false"; }, [&](const std::string& v) { ifnr = (v == "true" || v == "1"); actuateIFNR(); }, httpdebug::procfs::Type::Bool);
        httpdebug::procfs::registerEndpoint(path + "/snr_chart", [&]() { return snrChartWidget ? "true" : "false"; }, [&](const std::string& v) { snrChartWidget = (v == "true" || v == "1"); }, httpdebug::procfs::Type::Bool);
        httpdebug::procfs::registerEndpoint(path + "/cpu_usage", [&]() { return std::to_string(ifnrProcessor.percentUsage.load()); }, nullptr, httpdebug::procfs::Type::Int);
        httpdebug::procfs::registerEndpoint(path + "/reduction_db", [&]() { return std::to_string(reductionDb); }, [&](const std::string& v) {
            reductionDb = std::clamp(std::stof(v), 0.0f, 24.0f);
            ifnrProcessor.setReduction(reductionDb);
        }, httpdebug::procfs::Type::Float);
        httpdebug::procfs::registerEndpoint(path + "/acceleration", [&]() { return acceleration ? "true" : "false"; }, [&](const std::string& v) {
            acceleration = (v == "true" || v == "1");
            ifnrProcessor.setAcceleration(acceleration);
        }, httpdebug::procfs::Type::Bool);
        httpdebug::procfs::registerEndpoint(path + "/backend", [&]() { return ifnrProcessor.usesApple() ? "Apple Accelerate" : "FFTW"; }, nullptr, httpdebug::procfs::Type::String);
    }

    ~NRModule() {
        std::string path = "/modules/noise_reduction_logmmse/" + name;
        httpdebug::procfs::unregister(path + "/baseband_nr");
        httpdebug::procfs::unregister(path + "/snr_chart");
        httpdebug::procfs::unregister(path + "/cpu_usage");
        httpdebug::procfs::unregister(path + "/reduction_db");
        httpdebug::procfs::unregister(path + "/acceleration");
        httpdebug::procfs::unregister(path + "/backend");
        if (enabled) { enabled = false; updateBindings(); }
        gui::menu.removeEntry(name);
    }

    void postInit() {}

    void enable() {
        if (!enabled) {
            enabled = true;
            ifnrProcessor.setDisableCpuDeactivation(disableCpuDeactivation);
            updateBindings();
            actuateIFNR();
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
    float reductionDb = 12.0f;
    bool acceleration = true;

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

        actuateAFNR();
    }

    void detachAFFromRadio(const std::string& instanceName) {
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
        bool shouldRun = enabled && ifnr;
        if (ifnrProcessor.bypass != !shouldRun) {
            ifnrProcessor.bypass = !shouldRun;
            sigpath::iqFrontEnd.togglePreprocessor(&ifnrProcessor, shouldRun);
        }
    }

    void menuHandler() {
        float menuWidth = ImGui::GetContentRegionAvail().x;
        if (ImGui::Checkbox("Baseband NR##_sdrpp_if_nr", &ifnr)) {
            //            sigpath::signalPath.setWidebandNR(_this->widebandNR);
            config.acquire();
            config.conf["IFNR"] = ifnr;
            config.release(true);
            actuateIFNR();
        }
        ImGui::SameLine();
        if (ifnrProcessor.stoppedForCpu() && ifnr) {
            ifnr = false;
            config.acquire();
            config.conf["IFNR"] = ifnr;
            config.release(true);
            actuateIFNR();
        }
        if (ifnrProcessor.stoppedForCpu()) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0, 0, 1.0f));
            ImGui::TextUnformatted("Slow CPU. Reduce sample rate.");
            ImGui::PopStyleColor(1);
        }
        else {
            // show cpu usage
            if (ifnrProcessor.percentUsage >= 0) {
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

        ImGui::SetNextItemWidth(menuWidth);
        if (ImGui::SliderFloat("##baseband_reduction", &reductionDb, 0.0f, 24.0f, "Reduction: %.0f dB")) {
            ifnrProcessor.setReduction(reductionDb);
            config.acquire();
            config.conf["BasebandReductionDB"] = reductionDb;
            config.release(true);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Maximum noise attenuation. Start at 12 dB; lower it for weak signals or digital decoding.\n0 dB keeps the signal level unchanged, with the NR processing delay.");
        }
#ifdef __APPLE__
        if (ImGui::Checkbox("Automatic acceleration##baseband", &acceleration)) {
            ifnrProcessor.setAcceleration(acceleration);
            config.acquire();
            config.conf["BasebandAcceleration"] = acceleration;
            config.release(true);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Use Apple Accelerate for FFT and vector math when available.\nAutomatically falls back to FFTW. Disable to use FFTW directly.");
        }
#endif
        if (ifnr && ifnrProcessor.latencySamples() > 0) {
            ImGui::TextDisabled("%s | %.1f ms", ifnrProcessor.usesApple() ? "Apple Accelerate" : "FFTW",
                1000.0 * ifnrProcessor.latencySamples() / sigpath::iqFrontEnd.getSampleRate());
        }

        for (auto [k, v] : afnrProcessors2) {
            if (ImGui::Checkbox(("Audio NR2 " + k + "##_radio_omlsa_nr_" + k).c_str(), &v->allowed)) {
                actuateAFNR();
                config.acquire();
                config.conf["AF_NR2_" + k] = v->allowed;
                config.release(true);
            }
            ImGui::SameLine();
            ImGui::Text("%0.01f", 32767.0 / v->scaled);
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
