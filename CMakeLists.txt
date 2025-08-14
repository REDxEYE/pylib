cmake_minimum_required(VERSION 3.26...3.27)
project(pylib C CXX)

set(CMAKE_CXX_STANDARD 20)

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
        src/vpk_class.cpp
        src/stb_impl.cpp
        src/etc.cpp
        ext/tinyexr/deps/miniz/miniz.c
)
set_target_properties(pylib PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(pylib PRIVATE Python3::SABIModule libzstd lz4_static meshoptimizer VTFLib20)
if(WIN32)
    target_link_libraries(pylib PRIVATE Shlwapi)
endif()
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_link_directories(pylib PRIVATE C:/Users/RED/AppData/Local/Programs/Python/Python313/libs)
endif ()
target_include_directories(pylib PRIVATE ./include ext/tinyexr ext/vtflib/src ext/tinyexr ext/tinyexr/deps/miniz)
target_include_directories(pylib PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/ext/vtflib/src")