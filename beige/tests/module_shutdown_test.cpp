#include <module.h>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <thread>
#include <vector>

namespace {
ModuleManager* manager;
std::atomic<int> workers{0};
std::vector<int> destroyed;
int ended = 0;
struct Instance : ModuleManager::Instance {
    int kind;
    std::atomic<bool> stop{false};
    std::thread worker;
    explicit Instance(int value) : kind(value) {
        if (kind == 0) {
            ++workers;
            worker = std::thread([this] {
                while (!stop) std::this_thread::sleep_for(std::chrono::milliseconds(1));
                --workers;
            });
        }
    }
    ~Instance() override {
        if (kind == 0) assert(manager->instances.count("A Radio") && manager->instances.count("B Device"));
        if (kind == 1) assert(manager->instances.count("B Device"));
        stop = true;
        if (worker.joinable()) worker.join();
        destroyed.push_back(kind);
    }
    void postInit() override {}
    void enable() override {}
    void disable() override {}
    bool isEnabled() override { return true; }
};
void end() { assert(workers == 0 && manager->instances.empty()); ++ended; }
void remove(ModuleManager::Instance* instance) { delete instance; }
}
int run() {
    ModuleManager local;
    manager = &local;
    ModuleManager::ModuleInfo_t consumer{"worker", "", "", 1, 0, 0, -1};
    ModuleManager::ModuleInfo_t radio{"radio", "", "", 1, 0, 0, -1};
    ModuleManager::ModuleInfo_t device{"audio_sink", "", "", 1, 0, 0, -1};
    auto add = [&](const char* name, ModuleManager::ModuleInfo_t* info, int kind) {
        ModuleManager::Module_t module{};
        module.info = info; module.end = end; module.deleteInstance = remove;
        local.modules[info->name] = module;
        local.instances[name] = {module, new Instance(kind)};
    };
    // Alphabetical deletion would remove Radio and its provider before the worker.
    add("A Radio", &radio, 1);
    add("B Device", &device, 2);
    add("Z Consumer", &consumer, 0);
    local.shutdown();
    assert((destroyed == std::vector<int>{0, 1, 2}));
    assert(workers == 0 && ended == 3 && local.instances.empty() && local.modules.size() == 3);
    return 0;
}
int main() {
    // As with the other core-linked DSP tests, no GUI was initialized here.
    // Finish all local ownership checks before bypassing unrelated GUI globals.
    std::_Exit(run());
}
