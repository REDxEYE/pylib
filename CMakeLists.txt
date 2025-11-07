cmake_minimum_required(VERSION 3.26...3.27)
project(pylib C CXX)
set(CMAKE_CXX_STANDARD 20)

include(CheckCXXCompilerFlag)

function(target_add_flags_if_supported tgt scope)
    foreach(flag IN LISTS ARGN)
        # Generate a unique variable per-flag/compiler/arch to cache the test
        string(MD5 key "${flag}-${CMAKE_CXX_COMPILER_ID}-${CMAKE_SYSTEM_PROCESSOR}")
        set(var "HAVE_${key}")
        check_cxx_compiler_flag("${flag}" ${var})
        if(${var})
            target_compile_options(${tgt} ${scope} "${flag}")
        endif()
    endforeach()
endfunction()


find_package(Python3 REQUIRED COMPONENTS Development.SABIModule)
add_subdirectory(ext/zstd/build/cmake)
add_subdirectory(ext/lz4/build/cmake)
add_subdirectory(ext/meshoptimizer)
set(TINYEXR_BUILD_SAMPLE OFF)
add_subdirectory(ext/tinyexr)
set(USE_LIBTXC_DXTN ON)
set(BUILD_SHARED_LIBS OFF)
add_subdirectory(ext/vtflib)

get_target_property(SABI_DEBUG Python3::SABIModule IMPORTED_IMPLIB_DEBUG)
get_target_property(SABI_REL Python3::SABIModule IMPORTED_IMPLIB_RELEASE)

set_target_properties(Python3::SABIModule PROPERTIES MAP_IMPORTED_CONFIG_DEBUG ";RELEASE")

message(STATUS "Python include dir: ${Python3_INCLUDE_DIRS}")
message(STATUS "Python library:    ${Python3_LIBRARIES}")
message(STATUS "Python version:    ${Python3_VERSION}")
message(STATUS "Python interpreter: ${Python3_EXECUTABLE}")

message(STATUS "SABI Debug lib:   ${SABI_DEBUG}")
message(STATUS "SABI Release lib: ${SABI_REL}")

add_library(stbiw_obj OBJECT src/stb_impl.cpp ext/tinyexr/deps/miniz/miniz.c)
target_include_directories(stbiw_obj PRIVATE ./include ext/tinyexr ext/tinyexr/deps/miniz)
set_target_properties(stbiw_obj PROPERTIES POSITION_INDEPENDENT_CODE ON)
if (CMAKE_BUILD_TYPE STREQUAL "Debug")

else ()
    if (MSVC)
        # Always-safe opt flags on MSVC
        target_add_flags_if_supported(stbiw_obj PRIVATE /O2 /GL /Oi /Ot /fp:precise /GS- /Gy /Gw)

        # Only try AVX2 on x86/x64 MSVC toolsets
        if (CMAKE_SYSTEM_PROCESSOR MATCHES "AMD64|x86_64|x86|i.86")
            target_add_flags_if_supported(stbiw_obj PRIVATE /arch:AVX2)
        endif()

    else()
        # General opts for Clang/GCC
        target_add_flags_if_supported(stbiw_obj PRIVATE
                -O3 -fomit-frame-pointer -funroll-loops -fstrict-aliasing
        )

        # LTO differs by platform/compiler; try thin-LTO on Apple Clang
        if (APPLE)
            target_add_flags_if_supported(stbiw_obj PRIVATE -flto=thin)
        else()
            target_add_flags_if_supported(stbiw_obj PRIVATE -flto)
        endif()

        # CPU-specific flags by architecture
        if (CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|amd64|x86|i.86")
            target_add_flags_if_supported(stbiw_obj PRIVATE
                    -mavx2 -mfma -mbmi -mbmi2 -mlzcnt -mpopcnt
            )
        elseif (CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
            # Apple/ARM: NEON is default; avoid x86 flags. Optional: tune for host CPU.
            target_add_flags_if_supported(stbiw_obj PRIVATE -mcpu=native)
            # (You usually don't need -mfpu=neon on Apple/ARM; it’s implied.)
        endif()
    endif()
endif ()


Python3_add_library(pylib MODULE USE_SABI 3.10 WITH_SOABI
        src/module.cpp
        src/utils.cpp
        src/modules/compression_module.cpp
        src/modules/mesh_module.cpp
        src/modules/image_module.cpp
        src/modules/vtf_module.cpp
        src/vpk_glob_iterator.cpp
        src/vtf_class.cpp
        src/lz4_chaindecoder_class.cpp
        src/lz4_chainencoder_class.cpp
        src/vpk_class.cpp
        src/etc.cpp
)
target_compile_definitions(pylib PRIVATE BCDEC_BC4BC5_PRECISE)
set_target_properties(pylib PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(pylib PRIVATE Python3::SABIModule libzstd lz4_static meshoptimizer VTFLib20 stbiw_obj)
if (WIN32)
    target_link_libraries(pylib PRIVATE Shlwapi)
endif ()
if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_link_directories(pylib PRIVATE C:/PROGRAMS/python311/libs/)
endif ()
target_include_directories(pylib PRIVATE ./include ext/tinyexr ext/vtflib/src ext/tinyexr/deps/miniz)
target_include_directories(pylib PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/ext/vtflib/src")

if (MSVC)
    target_link_options(pylib PRIVATE /LTCG)
else ()
    target_link_options(pylib PRIVATE -flto)
endif ()