# PT_Log library - Retro68 build configuration
#
# Build with:
#   mkdir build-68k && cd build-68k
#   cmake .. -DCMAKE_TOOLCHAIN_FILE=/Retro68-build/toolchain/m68k-apple-macos/cmake/retro68.toolchain.cmake
#   make ptlog
#
#   mkdir build-ppc && cd build-ppc
#   cmake .. -DCMAKE_TOOLCHAIN_FILE=/Retro68-build/toolchain/powerpc-apple-macos/cmake/retroppc.toolchain.cmake
#   make ptlog

cmake_minimum_required(VERSION 3.9)
project(ptlog C)

# Detect if we're building for Classic Mac via Retro68
# 68k uses CMAKE_SYSTEM_NAME="Retro68"
# PPC uses CMAKE_SYSTEM_NAME="RetroPPC"
if(CMAKE_SYSTEM_NAME STREQUAL "Retro68" OR CMAKE_SYSTEM_NAME STREQUAL "RetroPPC")
    set(PTLOG_PLATFORM_MAC TRUE)
    set(PTLOG_SOURCES pt_log_mac.c)
    message(STATUS "Building PT_Log for Classic Mac (${CMAKE_SYSTEM_NAME})")
else()
    set(PTLOG_PLATFORM_MAC FALSE)
    set(PTLOG_SOURCES pt_log_posix.c)
    message(STATUS "Building PT_Log for POSIX")
endif()

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../../include)

add_library(ptlog STATIC ${PTLOG_SOURCES})

if(PTLOG_PLATFORM_MAC)
    # Classic Mac: strict warnings
    target_compile_options(ptlog PRIVATE -Wall -Wextra -Werror -pedantic)
else()
    # POSIX: C99 with pthread
    target_compile_options(ptlog PRIVATE -Wall -Wextra -Werror -pedantic -std=c99)
    target_link_libraries(ptlog pthread)
endif()

install(TARGETS ptlog ARCHIVE DESTINATION lib)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/../../include/pt_log.h DESTINATION include)
