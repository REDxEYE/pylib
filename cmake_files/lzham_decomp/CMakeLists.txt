PROJECT(lzhamdecomp)
cmake_minimum_required(VERSION 3.16)
option(BUILD_X64 "build 64-bit" TRUE)

message("Initial BUILD_X64=${BUILD_X64}")
message("Initial CMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}")

if( NOT CMAKE_BUILD_TYPE )
  set( CMAKE_BUILD_TYPE Release )
endif( NOT CMAKE_BUILD_TYPE )

message( ${PROJECT_NAME} " build type: " ${CMAKE_BUILD_TYPE} )

if (BUILD_X64)
	message("Building 64-bit")
else()
	message("Building 32-bit")
endif(BUILD_X64)

set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g -Wall")
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g -Wall")

set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -Wall -O3 -fomit-frame-pointer -fexpensive-optimizations")
set(CMAKE_C_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -Wall")

set(SRC_LIST 
    ../../lib/lzham_codec/include/lzham_dynamic_lib.h
    ../../lib/lzham_codec/include/lzham_static_lib.h
    ../../lib/lzham_codec/include/lzham.h
    ../../lib/lzham_codec/include/lzham_exports.inc
    ../../lib/lzham_codec/include/zlib.h
    ../../lib/lzham_codec/lzhamdecomp/lzham_assert.cpp
    ../../lib/lzham_codec/lzhamdecomp/lzham_assert.h
    ../../lib/lzham_codec/lzhamdecomp/lzham_checksum.cpp
    ../../lib/lzham_codec/lzhamdecomp/lzham_checksum.h
    ../../lib/lzham_codec/lzhamdecomp/lzham_config.h
    ../../lib/lzham_codec/lzhamdecomp/lzham_core.h
    ../../lib/lzham_codec/lzhamdecomp/lzham_decomp.h
    ../../lib/lzham_codec/lzhamdecomp/lzham_helpers.h
    ../../lib/lzham_codec/lzhamdecomp/lzham_huffman_codes.cpp
    ../../lib/lzham_codec/lzhamdecomp/lzham_huffman_codes.h
    ../../lib/lzham_codec/lzhamdecomp/lzham_lzdecompbase.cpp
    ../../lib/lzham_codec/lzhamdecomp/lzham_lzdecompbase.h
    ../../lib/lzham_codec/lzhamdecomp/lzham_lzdecomp.cpp
    ../../lib/lzham_codec/lzhamdecomp/lzham_math.h
    ../../lib/lzham_codec/lzhamdecomp/lzham_mem.cpp
    ../../lib/lzham_codec/lzhamdecomp/lzham_mem.h
    ../../lib/lzham_codec/lzhamdecomp/lzham_platform.cpp
    ../../lib/lzham_codec/lzhamdecomp/lzham_platform.h
    ../../lib/lzham_codec/lzhamdecomp/lzham_prefix_coding.cpp
    ../../lib/lzham_codec/lzhamdecomp/lzham_prefix_coding.h
    ../../lib/lzham_codec/lzhamdecomp/lzham_symbol_codec.cpp
    ../../lib/lzham_codec/lzhamdecomp/lzham_symbol_codec.h
    ../../lib/lzham_codec/lzhamdecomp/lzham_timer.cpp
    ../../lib/lzham_codec/lzhamdecomp/lzham_timer.h
    ../../lib/lzham_codec/lzhamdecomp/lzham_traits.h
    ../../lib/lzham_codec/lzhamdecomp/lzham_types.h
    ../../lib/lzham_codec/lzhamdecomp/lzham_utils.h
    ../../lib/lzham_codec/lzhamdecomp/lzham_vector.cpp
    ../../lib/lzham_codec/lzhamdecomp/lzham_vector.h)

# -fno-strict-aliasing is *required* to compile LZHAM
set(GCC_COMPILE_FLAGS "-fno-strict-aliasing -D_LARGEFILE64_SOURCE=1 -D_FILE_OFFSET_BITS=64")

if (NOT BUILD_X64)
	set(GCC_COMPILE_FLAGS "${GCC_COMPILE_FLAGS} -m32")
endif()

set(CMAKE_EXE_LINKER_FLAGS  "${CMAKE_EXE_LINKER_FLAGS} ${GCC_LINK_FLAGS}")

set(CMAKE_C_FLAGS  "${CMAKE_C_FLAGS} ${GCC_COMPILE_FLAGS}")
set(CMAKE_C_FLAGS_RELEASE  "${CMAKE_C_FLAGS_RELEASE} ${GCC_COMPILE_FLAGS} -DNDEBUG")
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} ${GCC_COMPILE_FLAGS} -D_DEBUG")

set(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} ${GCC_COMPILE_FLAGS}")
set(CMAKE_CXX_FLAGS_RELEASE  "${CMAKE_CXX_FLAGS_RELEASE} ${GCC_COMPILE_FLAGS} -DNDEBUG")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} ${GCC_COMPILE_FLAGS} -D_DEBUG")

include_directories(
	../../lib/lzham_codec/lzhamdecomp
	../../lib/lzham_codec/include)

#set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin_linux)


add_library(${PROJECT_NAME} ${SRC_LIST})
target_compile_options(${PROJECT_NAME} PUBLIC -fPIC)
target_compile_definitions(${PROJECT_NAME} PUBLIC UINT16_MAX=0xFFFF)
target_compile_definitions(${PROJECT_NAME} PUBLIC UINT64_MAX=0xFFFFFFFFFFFFFFFF)



install(TARGETS ${PROJECT_NAME} LIBRARY DESTINATION lib${LIB_SUFFIX} ARCHIVE DESTINATION lib${LIB_SUFFIX} RUNTIME DESTINATION bin)
