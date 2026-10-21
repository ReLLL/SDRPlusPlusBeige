add_executable(beige_history_test history_test.cpp)
target_include_directories(beige_history_test PRIVATE "${SDRPP_CORE_ROOT}")
target_compile_features(beige_history_test PRIVATE cxx_std_17)
target_compile_options(beige_history_test PRIVATE $<$<CXX_COMPILER_ID:AppleClang,Clang,GNU>:-UNDEBUG>)
add_test(NAME beige_history COMMAND beige_history_test)
if(APPLE AND EXISTS "${CMAKE_BINARY_DIR}/beige_darwin_disconnect_test")
    add_test(NAME beige_usb_disconnect COMMAND "${CMAKE_BINARY_DIR}/beige_darwin_disconnect_test")
endif()
if(APPLE)
    set(BEIGE_NR_SRC "${CMAKE_SOURCE_DIR}/misc_modules/noise_reduction_logmmse/src")
    add_executable(beige_audio_nr_test audio_nr_test.cpp
        "${BEIGE_NR_SRC}/omlsa_mcra/Datablock_Read.cpp"
        "${BEIGE_NR_SRC}/omlsa_mcra/LSA_denoise.cpp"
        "${BEIGE_NR_SRC}/omlsa_mcra/G_calculate.cpp"
        "${BEIGE_NR_SRC}/omlsa_mcra/base4_fft.cpp"
        "${BEIGE_NR_SRC}/omlsa_mcra/Cordic.cpp")
    target_include_directories(beige_audio_nr_test PRIVATE "${SDRPP_CORE_ROOT}" "${BEIGE_NR_SRC}")
    target_compile_features(beige_audio_nr_test PRIVATE cxx_std_17)
    target_compile_options(beige_audio_nr_test PRIVATE -UNDEBUG -fsanitize=address -fno-omit-frame-pointer)
    target_link_options(beige_audio_nr_test PRIVATE -fsanitize=address)
    add_test(NAME beige_audio_nr COMMAND beige_audio_nr_test "${CMAKE_SOURCE_DIR}/root/res")
endif()
