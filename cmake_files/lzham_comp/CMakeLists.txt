PROJECT(lzhamcomp)
cmake_minimum_required(VERSION 2.8)
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
	../../lib/lzham_codec/lzhamcomp/lzham_lzbase.cpp
	../../lib/lzham_codec/lzhamcomp/lzham_lzbase.h
	../../lib/lzham_codec/lzhamcomp/lzham_lzcomp.cpp
	../../lib/lzham_codec/lzhamcomp/lzham_lzcomp_internal.cpp
	../../lib/lzham_codec/lzhamcomp/lzham_lzcomp_internal.h
	../../lib/lzham_codec/lzhamcomp/lzham_lzcomp_state.cpp
	../../lib/lzham_codec/lzhamcomp/lzham_match_accel.cpp
	../../lib/lzham_codec/lzhamcomp/lzham_match_accel.h
	../../lib/lzham_codec/lzhamcomp/lzham_null_threading.h
#	../../lib/lzham_codec/lzhamcomp/lzham_pthreads_threading.cpp
#	../../lib/lzham_codec/lzhamcomp/lzham_pthreads_threading.h
	../../lib/lzham_codec/lzhamcomp/lzham_threading.h
		)

if (WIN32)
  list (APPEND SRC_LIST ../../lib/lzham_codec/lzhamcomp/lzham_win32_threading.cpp)
endif ()

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
target_link_libraries(${PROJECT_NAME} lzhamdecomp)
install(TARGETS ${PROJECT_NAME} LIBRARY DESTINATION lib${LIB_SUFFIX} ARCHIVE DESTINATION lib${LIB_SUFFIX} RUNTIME DESTINATION bin)
