cmake_minimum_required(VERSION 3.13)
project(m17_decoder)

file(GLOB_RECURSE SRC "src/*.cpp" "src/*.c")

include(${SDRPP_MODULE_CMAKE})

target_include_directories(m17_decoder PRIVATE "src/")

if (MSVC)
    # Lib path
    target_include_directories(m17_decoder PRIVATE "C:/Program Files/codec2/include/")
    target_link_directories(m17_decoder PRIVATE "C:/Program Files/codec2/lib")

    target_link_libraries(m17_decoder PRIVATE libcodec2)
elseif (ANDROID)
    target_include_directories(m17_decoder PUBLIC
            ${SDR_KIT_ROOT}/${ANDROID_ABI}/include/codec2
    )

    target_link_libraries(m17_decoder PUBLIC
            ${SDR_KIT_ROOT}/${ANDROID_ABI}/lib/libcodec2.so
    )
else ()
    find_package(PkgConfig)

    pkg_check_modules(LIBCODEC2 REQUIRED codec2)

    # Some macOS Codec2 bottles omit this public header. Generate it locally
    # from the detected library version; never patch the global installation.
    if(APPLE)
        find_file(CODEC2_VERSION_HEADER codec2/version.h PATHS ${LIBCODEC2_INCLUDE_DIRS} NO_DEFAULT_PATH)
        if(NOT CODEC2_VERSION_HEADER)
            string(REPLACE "." ";" CODEC2_VERSION_PARTS "${LIBCODEC2_VERSION}")
            list(GET CODEC2_VERSION_PARTS 0 CODEC2_MAJOR)
            list(GET CODEC2_VERSION_PARTS 1 CODEC2_MINOR)
            configure_file("${CMAKE_SOURCE_DIR}/beige/cmake/codec2-version.h.in"
                "${CMAKE_CURRENT_BINARY_DIR}/compat/codec2/version.h" @ONLY)
            target_include_directories(m17_decoder PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/compat")
        endif()
    endif()

    target_include_directories(m17_decoder PRIVATE ${LIBCODEC2_INCLUDE_DIRS})
    target_include_directories(m17_decoder PRIVATE "/usr/include/codec2")
    target_link_directories(m17_decoder PRIVATE ${LIBCODEC2_LIBRARY_DIRS})
    target_link_libraries(m17_decoder PRIVATE ${LIBCODEC2_LIBRARIES})

    # Include it because for some reason pkgconfig doesn't look here?
    if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
        target_include_directories(m17_decoder PRIVATE "/usr/local/include")
    endif()
endif ()
