cmake_minimum_required(VERSION 3.13)
project(noise_reduction_logmmse)

file(GLOB SRC "src/*.cpp" "src/omlsa_mcra/*.cpp")

include(${SDRPP_MODULE_CMAKE})

target_include_directories(noise_reduction_logmmse PRIVATE "src/")
target_include_directories(noise_reduction_logmmse PRIVATE "src/omlsa_mcra")
target_include_directories(noise_reduction_logmmse PRIVATE "../../decoder_modules/radio/src")

set(BEIGE_ONNXRUNTIME_ROOT "" CACHE PATH "Optional private ONNX Runtime for neural audio NR")
if(BEIGE_ONNXRUNTIME_ROOT)
    find_library(BEIGE_ONNXRUNTIME_LIBRARY NAMES onnxruntime
        PATHS "${BEIGE_ONNXRUNTIME_ROOT}/lib" NO_DEFAULT_PATH REQUIRED)
    target_include_directories(noise_reduction_logmmse PRIVATE "${BEIGE_ONNXRUNTIME_ROOT}/include")
    target_compile_definitions(noise_reduction_logmmse PRIVATE BEIGE_NEURAL_AUDIO=1)
    target_link_libraries(noise_reduction_logmmse PRIVATE "${BEIGE_ONNXRUNTIME_LIBRARY}")
endif()
