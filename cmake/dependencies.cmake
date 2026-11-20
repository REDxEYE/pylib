include(FetchContent)

#set(USE_LIBTXC_DXTN ON)
set(BUILD_SHARED_LIBS OFF)
set(VTFLIB_BUILD_VTFCMD OFF)
set(VTFLIB_BUILD_VTFEDIT OFF)
FetchContent_Declare(
        VTFEdit
        QUIET
        GIT_REPOSITORY https://github.com/REDxEYE/VTFLib.git
        # Pinned for reproducible builds; bump deliberately.
        GIT_TAG 74e2388960cbb3c66301b815f0ca1de4f34e6019
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

# studiomdl_v2: the model compiler behind pylib.studiomdl. Its sources use x86 SSE intrinsics and x87 assembly, so it
# only builds for x86_64; Apple arm64 builds (the arm64 half of the macOS universal binary) leave pylib.studiomdl out.
# Local development against a checkout: -DFETCHCONTENT_SOURCE_DIR_STUDIOMDL_V2=<path>.
if (APPLE AND (CMAKE_OSX_ARCHITECTURES MATCHES "arm64"
        OR (NOT CMAKE_OSX_ARCHITECTURES AND CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")))
    set(PYLIB_STUDIOMDL_DEFAULT OFF)
else ()
    set(PYLIB_STUDIOMDL_DEFAULT ON)
endif ()
option(PYLIB_WITH_STUDIOMDL "Build the pylib.studiomdl model compiler module" ${PYLIB_STUDIOMDL_DEFAULT})

if (PYLIB_WITH_STUDIOMDL)
    FetchContent_Declare(
            studiomdl_v2
            GIT_REPOSITORY https://github.com/REDxEYE/studiomdl_v2.git
            # Pinned for reproducible builds; bump deliberately.
            GIT_TAG a45d2471
    )
    # Must follow meshoptimizer: studiomdl links the host's meshoptimizer target when it exists and would otherwise
    # define its own (vendored) one, which then collides with ours.
    FetchContent_MakeAvailable(studiomdl_v2)
endif ()