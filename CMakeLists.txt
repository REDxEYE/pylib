cmake_minimum_required(VERSION 3.16)
project(pylib)
set(CMAKE_CXX_STANDARD 17)

find_package(Python3 REQUIRED COMPONENTS Development NumPy)

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
        lib/detex/raw.c
        )

set(LZ4_SOURCES
        lib/lz4/lib/lz4.c
        lib/lz4/lib/lz4frame.c
        lib/lz4/lib/lz4hc.c
        lib/lz4/lib/xxhash.c
        )

set(PYLIB_SOURCES
        main.cpp
        compressed_index_buffer.cpp
        compressed_vertex_buffer.cpp
        )

add_library(pylib SHARED ${PYLIB_SOURCES} ${DETEX_SOURCES} ${LZ4_SOURCES})

target_include_directories(pylib PRIVATE ${Python3_INCLUDE_DIRS} lib/detex lib/lz4/lib lib/span/include/tcb)
target_link_libraries(pylib PRIVATE ${Python3_LIBRARIES})
target_link_options(pylib PRIVATE -static)

set_target_properties(pylib PROPERTIES CMAKE_C_STANDARD 90)
set_target_properties(pylib PROPERTIES PREFIX "")
set_target_properties(pylib PROPERTIES SUFFIX ".pyd")
