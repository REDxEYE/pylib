cmake_minimum_required(VERSION 3.16)
project(pylib)
set(CMAKE_CXX_STANDARD 17)
set (Python3_FIND_ABI "ON" "ON" "ON")
find_package(Python REQUIRED COMPONENTS Development Interpreter )

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
        src/main.cpp
        src/compressed_mesh_module/compressed_index_buffer.cpp
        src/compressed_mesh_module/compressed_vertex_buffer.cpp
        src/texture_module/texture_decompression.cpp
        )

add_library(pylib SHARED ${PYLIB_SOURCES} ${DETEX_SOURCES} ${LZ4_SOURCES})
target_compile_definitions(pylib
        PRIVATE
        Py_LIMITED_API
        )
target_include_directories(pylib PRIVATE ${Python_INCLUDE_DIRS} include lib/detex lib/lz4/lib lib/span/include/tcb)
if (MSVC)
    target_link_directories(pylib PUBLIC ${Python_LIBRARY_DIRS})
    target_link_libraries(pylib PRIVATE python3)
    target_compile_definitions(pylib PUBLIC _CRT_SECURE_NO_WARNINGS)
    target_compile_options(pylib PUBLIC /EHs)
    set_target_properties(pylib PROPERTIES SUFFIX ".pyd")
else()
    target_link_libraries(pylib PRIVATE ${Python_LIBRARIES})
#    target_link_options(pylib PRIVATE -static)
endif ()
#set_target_properties(pylib PROPERTIES CMAKE_C_STANDARD 90)
set_target_properties(pylib PROPERTIES PREFIX "")

