#include "neural_model_path.h"
#include <cassert>
#include <chrono>
#include <fstream>
#include <iostream>

int main() {
    namespace fs = std::filesystem;
    const auto root = fs::temp_directory_path() / ("beige-model-path-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto app = root / "Moved Beige.app/Contents";
    const auto overlay = root / "custom bandplan";
    const auto bundled = app / "Resources/models/gtcrn.onnx";
    const auto configured = overlay / "models/gtcrn.onnx";
    fs::create_directories(bundled.parent_path());
    fs::create_directories(configured.parent_path());
    std::ofstream(bundled).put('x');
    const auto executable = app / "MacOS/sdrpp";
    assert(dsp::resolveNeuralModel(overlay, executable) == bundled.string());
    std::ofstream(configured).put('y');
    assert(dsp::resolveNeuralModel(overlay, executable) == bundled.string());
    assert(dsp::resolveNeuralModel(overlay, root / "bin/sdrpp") == configured.string());
    fs::remove(bundled);
    assert(dsp::resolveNeuralModel(overlay, executable) == configured.string());
    fs::remove(configured);
    assert(dsp::resolveNeuralModel(overlay, executable) == configured.string());
    fs::remove_all(root);
    std::cout << "Packaged model, custom overlays, moved app and unbundled fallback passed.\n";
}
