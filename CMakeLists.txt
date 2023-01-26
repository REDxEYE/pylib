cmake_minimum_required(VERSION 3.18)
project(pylib)
set(CMAKE_CXX_STANDARD 11)
find_package(Threads REQUIRED)

set(DETEX_SOURCES
        lib/detex/bits.c
        lib/detex/bptc-tables.c
        lib/detex/clamp.c
        lib/detex/convert.c
        lib/detex/dds.c
        lib/detex/decompress-bc.c
        lib/detex/decompress-bptc.c
        lib/detex/decompress-bptc-float.c
        lib/detex/decompress-eac.c
        lib/detex/decompress-etc.c
        lib/detex/decompress-rgtc.c
        lib/detex/decompress-rgtc.c
        lib/detex/division-tables.c
        lib/detex/file-info.c
        lib/detex/texture.c
        lib/detex/half-float.c
        lib/detex/hdr.c
        lib/detex/ktx.c
        lib/detex/misc.c
        #        lib/detex/raw.c
        )

set(LZ4_SOURCES
        lib/lz4/lib/lz4.c
        lib/lz4/lib/lz4frame.c
        lib/lz4/lib/lz4hc.c
        lib/lz4/lib/xxhash.c
        )

set(PYLIB_SOURCES
        src/main.cpp
        src/lz4_module.cpp
        src/compressed_mesh_module/compressed_index_buffer.cpp
        src/compressed_mesh_module/compressed_vertex_buffer.cpp
        src/texture_decompression.cpp
        src/zstd_module.cpp
        )



set(BUILD_SHARED_LIBS OFF)
set(CMAKE_EXE_LINKER_FLAGS "-static")

add_subdirectory(cmake_files/lzham_decomp)
add_subdirectory(cmake_files/lzham_comp)

set(PYLIB_ROOT_DIR ${CMAKE_SOURCE_DIR})
set(LIBRARY_DIR ${CMAKE_SOURCE_DIR}/lib/zstd/lib)
list(APPEND CMAKE_MODULE_PATH ${PYLIB_ROOT_DIR}/lib/zstd/build/cmake/CMakeModules)
SET(ZSTD_BUILD_SHARED OFF)
SET(ZSTD_BUILD_STATIC ON)
add_subdirectory(lib/zstd/build/cmake/lib)


add_library(pylib SHARED ${PYLIB_SOURCES} ${DETEX_SOURCES} ${LZ4_SOURCES} ${ZSTD_SOURCES} lib/lzham_codec/lzhamdll/lzham_api.cpp)

target_include_directories(pylib PUBLIC include lib/detex lib/bcdec lib/lz4/lib lib/span/include/tcb lib/zstd/lib lib/lzham_codec/lzhamdecomp lib/lzham_codec/lzhamcomp lib/lzham_codec/include)
if (MSVC)
    add_compile_definitions(LZHAM_EXPORTS ZSTD_DLL_EXPORT=1)
    target_link_libraries(pylib PUBLIC lzhamdecomp lzhamcomp libzstd_static)
    target_compile_definitions(pylib PUBLIC _CRT_SECURE_NO_WARNINGS)
    target_compile_options(pylib PUBLIC /EHs /std:c++latest /Zc:strictStrings-)
else ()
#    add_compile_definitions(LZHAM_EXPORTS)
    target_link_libraries(pylib PUBLIC lzhamdecomp lzhamcomp libzstd_static -static-libgcc -static-libstdc++)
endif ()
set_target_properties(pylib PROPERTIES PREFIX "")

