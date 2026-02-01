cmake_minimum_required(VERSION 3.10)
project(PeerTalk C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Include directories
include_directories(include src/core)

# Platform detection
if(UNIX AND NOT APPLE)
    set(PT_PLATFORM "POSIX")
    add_definitions(-DPT_PLATFORM_POSIX)
elseif(APPLE)
    set(PT_PLATFORM "POSIX")
    add_definitions(-DPT_PLATFORM_POSIX)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Retro68")
    set(PT_PLATFORM "MACTCP")
    add_definitions(-DPT_PLATFORM_MACTCP)
elseif(CMAKE_SYSTEM_NAME STREQUAL "RetroPPC")
    set(PT_PLATFORM "OT")
    add_definitions(-DPT_PLATFORM_OT)
else()
    message(WARNING "Unknown platform, defaulting to POSIX")
    set(PT_PLATFORM "POSIX")
    add_definitions(-DPT_PLATFORM_POSIX)
endif()

# Common definitions
add_definitions(-DPT_LOG_ENABLED)

# Core sources (added incrementally as sessions complete)
set(CORE_SRCS "src/core/pt_version.c")

# Platform-specific sources (added incrementally)
if(PT_PLATFORM STREQUAL "POSIX")
    set(POSIX_SRCS "")
    set(PLATFORM_SRCS ${POSIX_SRCS})
elseif(PT_PLATFORM STREQUAL "MACTCP")
    set(MACTCP_SRCS "")
    set(PLATFORM_SRCS ${MACTCP_SRCS})
elseif(PT_PLATFORM STREQUAL "OT")
    set(OT_SRCS "")
    set(PLATFORM_SRCS ${OT_SRCS})
endif()

# PeerTalk library (starts empty, files added as sessions complete)
set(PEERTALK_SRCS ${CORE_SRCS} ${PLATFORM_SRCS})

if(PEERTALK_SRCS)
    add_library(peertalk STATIC ${PEERTALK_SRCS})
    target_include_directories(peertalk PUBLIC include PRIVATE src/core)
    target_compile_definitions(peertalk PRIVATE PT_LOG_ENABLED)

    if(PT_PLATFORM STREQUAL "POSIX")
        target_compile_definitions(peertalk PRIVATE PT_PLATFORM_POSIX)
        target_link_libraries(peertalk pthread)
    elseif(PT_PLATFORM STREQUAL "MACTCP")
        target_compile_definitions(peertalk PRIVATE PT_PLATFORM_MACTCP)
    elseif(PT_PLATFORM STREQUAL "OT")
        target_compile_definitions(peertalk PRIVATE PT_PLATFORM_OT)
    endif()
else()
    message(STATUS "No source files yet - PeerTalk library will be added as sessions complete")
endif()

# Tests added as sessions complete
