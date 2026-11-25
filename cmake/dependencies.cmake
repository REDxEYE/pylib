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
        GIT_TAG 1b2c2a3a676823d73aeec68a3b0d294dec921bb1
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

# studiomdl_v2: the model compiler behind pylib.studiomdl; builds for x86_64 and arm64.
# Local development against a checkout: -DFETCHCONTENT_SOURCE_DIR_STUDIOMDL_V2=<path>.
option(PYLIB_WITH_STUDIOMDL "Build the pylib.studiomdl model compiler module" ON)

if (PYLIB_WITH_STUDIOMDL)
    FetchContent_Declare(
            studiomdl_v2
            GIT_REPOSITORY https://github.com/REDxEYE/studiomdl_v2.git
            # Pinned for reproducible builds; bump deliberately.
            GIT_TAG 4804211ac67953b7543e658c6b94b559f862c891
    )
    # Must follow meshoptimizer: studiomdl links the host's meshoptimizer target when it exists and would otherwise
    # define its own (vendored) one, which then collides with ours.
    FetchContent_MakeAvailable(studiomdl_v2)
endif ()