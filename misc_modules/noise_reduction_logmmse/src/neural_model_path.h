#pragma once
#include <filesystem>
#include <string>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#include <vector>
#endif

namespace dsp {
inline std::string resolveNeuralModel(const std::filesystem::path& resources,
    const std::filesystem::path& executable) {
    const auto macos = executable.parent_path();
    if (macos.filename() == "MacOS" && macos.parent_path().filename() == "Contents") {
        const auto bundled = macos.parent_path() / "Resources/models/gtcrn.onnx";
        std::error_code error;
        if (std::filesystem::is_regular_file(bundled, error)) return bundled.string();
    }
    return (resources / "models/gtcrn.onnx").string();
}

inline std::string neuralModelPath(const std::string& resources) {
    std::filesystem::path executable;
#ifdef __APPLE__
    uint32_t size = 1024;
    std::vector<char> buffer(size);
    if (_NSGetExecutablePath(buffer.data(), &size) != 0) buffer.resize(size);
    if (_NSGetExecutablePath(buffer.data(), &size) == 0) {
        std::error_code error;
        executable = std::filesystem::canonical(buffer.data(), error);
        if (error) executable.clear();
    }
#endif
    // User band-plan overlays may predate packaged models. Prefer this app's model.
    return resolveNeuralModel(resources, executable);
}
}
