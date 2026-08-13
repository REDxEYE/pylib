include(FetchContent)

#set(USE_LIBTXC_DXTN ON)
set(BUILD_SHARED_LIBS OFF)
set(VTFLIB_BUILD_VTFCMD OFF)
set(VTFLIB_BUILD_VTFEDIT OFF)
FetchContent_Declare(
        VTFEdit
        QUIET
        GIT_REPOSITORY https://github.com/REDxEYE/VTFLib.git
        GIT_TAG master
        GIT_SHALLOW TRUE
)


FetchContent_Declare(
        lz4
        QUIET
        GIT_REPOSITORY https://github.com/lz4/lz4.git
        GIT_TAG v1.10.0
        SOURCE_SUBDIR build/cmake
        GIT_SHALLOW TRUE
)

FetchContent_Declare(
        meshoptimizer
        QUIET
        GIT_REPOSITORY https://github.com/zeux/meshoptimizer.git
        GIT_TAG v1.2
        GIT_SHALLOW TRUE
)


set(TINYEXR_BUILD_SAMPLE OFF)
FetchContent_Declare(
        tinyexr
        QUIET
        GIT_REPOSITORY https://github.com/syoyo/tinyexr.git
        GIT_TAG v3.2.0
        GIT_SHALLOW TRUE
        SOURCE_SUBDIR _do_not_build
)

FetchContent_Declare(
        zstd
        GIT_REPOSITORY "https://github.com/facebook/zstd"
        GIT_TAG v1.5.7
        SOURCE_SUBDIR build/cmake
        GIT_SHALLOW TRUE
)

FetchContent_Declare(
        miniz
        GIT_REPOSITORY https://github.com/richgel999/miniz.git
        GIT_TAG 3.1.2
        GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(miniz)
FetchContent_MakeAvailable(meshoptimizer)
set_target_properties(meshoptimizer PROPERTIES POSITION_INDEPENDENT_CODE ON)

SET(ZSTD_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
SET(ZSTD_BUILD_CONTRIB OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(zstd)

FetchContent_MakeAvailable(tinyexr)
FetchContent_MakeAvailable(VTFEdit)
FetchContent_MakeAvailable(lz4)

FetchContent_MakeAvailable(tinyexr)

add_library(tinyexr STATIC
        "${tinyexr_SOURCE_DIR}/tinyexr.cc"
)

target_include_directories(tinyexr
        PUBLIC "${tinyexr_SOURCE_DIR}"
)

target_compile_definitions(tinyexr
        PRIVATE TINYEXR_USE_MINIZ=1
)

target_link_libraries(tinyexr
        PRIVATE miniz
)