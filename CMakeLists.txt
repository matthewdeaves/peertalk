cmake_minimum_required(VERSION 3.14)
project(peertalk C)

include(FetchContent)

option(PEERTALK_BUILD_TESTS "Build PeerTalk test and demo applications" ON)

# Platform detection
if(NOT DEFINED PT_PLATFORM)
    if(CMAKE_SYSTEM_NAME MATCHES "Retro68")
        set(PT_PLATFORM "MACTCP")
    elseif(CMAKE_SYSTEM_NAME MATCHES "RetroPPC")
        set(PT_PLATFORM "OT")
    else()
        set(PT_PLATFORM "POSIX")
    endif()
endif()

message(STATUS "PeerTalk platform: ${PT_PLATFORM}")

# --- clog dependency ---
# If the clog target already exists (parent project set it up), use it.
# Otherwise: use local dir if provided, or fetch from GitHub.
if(NOT TARGET clog)
    if(NOT DEFINED CLOG_DIR AND DEFINED ENV{CLOG_DIR})
        set(CLOG_DIR "$ENV{CLOG_DIR}")
    endif()

    if(DEFINED CLOG_DIR)
        message(STATUS "Using local clog: ${CLOG_DIR}")
        set(CLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        add_subdirectory(${CLOG_DIR} ${CMAKE_BINARY_DIR}/clog)
    else()
        message(STATUS "Fetching clog from GitHub")
        FetchContent_Declare(clog
            GIT_REPOSITORY https://github.com/matthewdeaves/clog.git
            GIT_TAG        main
            GIT_SHALLOW    TRUE
        )
        set(CLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(clog)
    endif()
endif()

# Core source files (always compiled)
set(CORE_SOURCES
    src/core/pt_core.c
    src/core/pt_memory.c
    src/core/pt_discovery.c
    src/core/pt_messaging.c
)

# Platform-specific source files
if(PT_PLATFORM STREQUAL "POSIX")
    set(PLATFORM_SOURCES src/platform/posix/pt_posix.c)
    set(PLATFORM_DEFINE PT_PLATFORM_POSIX)
elseif(PT_PLATFORM STREQUAL "MACTCP")
    set(PLATFORM_SOURCES src/platform/mactcp/pt_mactcp.c)
    set(PLATFORM_DEFINE PT_PLATFORM_MACTCP)
elseif(PT_PLATFORM STREQUAL "OT")
    set(PLATFORM_SOURCES src/platform/opentransport/pt_ot.c)
    set(PLATFORM_DEFINE PT_PLATFORM_OT)
else()
    message(FATAL_ERROR "Unknown PT_PLATFORM: ${PT_PLATFORM}")
endif()

# Static library
add_library(peertalk STATIC ${CORE_SOURCES} ${PLATFORM_SOURCES})
target_include_directories(peertalk PUBLIC include)
target_include_directories(peertalk PRIVATE src/core)
target_compile_definitions(peertalk PRIVATE ${PLATFORM_DEFINE})

# C standard and warnings
if(PT_PLATFORM STREQUAL "POSIX")
    set_target_properties(peertalk PROPERTIES C_STANDARD 90)
    target_compile_options(peertalk PRIVATE -Wall -Wextra)
endif()

# Retro68: suppress warnings that Classic Mac headers trigger
if(PT_PLATFORM STREQUAL "MACTCP" OR PT_PLATFORM STREQUAL "OT")
    target_compile_options(peertalk PRIVATE -Wall -Wextra
        -Wno-unknown-pragmas -Wno-unused-parameter)
endif()

# Link clog (target or pre-set cache variable from parent project)
target_link_libraries(peertalk PRIVATE clog)

# OT needs both the application-level wrappers and the core library,
# plus the internet provider library. Library names differ by architecture.
if(PT_PLATFORM STREQUAL "OT")
    if(CMAKE_SYSTEM_NAME MATCHES "Retro68")
        # 68k OT libraries + SLM dispatch stubs
        find_library(OT_APP_LIB OpenTransportApp)
        find_library(OT_LIB OpenTransport)
        find_library(OT_INET_LIB OpenTptInet)
        # libOpenTransport.a references SLM dispatch symbols resolved at
        # load time on real Macs. Provide stubs to satisfy the linker.
        add_library(ot_slm_stubs STATIC src/platform/opentransport/ot_slm_stubs.c)
    else()
        # PPC OT libraries
        find_library(OT_APP_LIB OpenTransportAppPPC)
        find_library(OT_LIB OpenTransportLib)
        find_library(OT_INET_LIB OpenTptInternetLib)
    endif()
    if(OT_APP_LIB)
        target_link_libraries(peertalk PRIVATE ${OT_APP_LIB})
    endif()
    if(OT_LIB)
        target_link_libraries(peertalk PRIVATE ${OT_LIB})
    endif()
    if(OT_INET_LIB)
        target_link_libraries(peertalk PRIVATE ${OT_INET_LIB})
    endif()
    if(TARGET ot_slm_stubs)
        target_link_libraries(peertalk PRIVATE ot_slm_stubs)
    endif()
endif()

# Test and demo applications (skip when used as a subdirectory)
if(NOT PEERTALK_BUILD_TESTS)
    return()
endif()

# POSIX test executables
if(PT_PLATFORM STREQUAL "POSIX")
    set(TEST_APPS test_fast test_reliable test_chat test_lifecycle test_multi test_seam)
    foreach(TEST_APP ${TEST_APPS})
        add_executable(${TEST_APP} tests/${TEST_APP}.c)
        target_include_directories(${TEST_APP} PRIVATE include tests)
        target_compile_definitions(${TEST_APP} PRIVATE ${PLATFORM_DEFINE})
        set_target_properties(${TEST_APP} PROPERTIES C_STANDARD 11)
        target_compile_options(${TEST_APP} PRIVATE -Wall -Wextra)
        target_link_libraries(${TEST_APP} PRIVATE peertalk clog)
    endforeach()

    # Minimal clog test (no peertalk dependency)
    add_executable(test_clog_minimal tests/test_clog_minimal.c)
    target_compile_definitions(test_clog_minimal PRIVATE ${PLATFORM_DEFINE})
    set_target_properties(test_clog_minimal PROPERTIES C_STANDARD 11)
    target_link_libraries(test_clog_minimal PRIVATE clog)

endif()

# Retro68 application targets (.bin output)
if(PT_PLATFORM STREQUAL "MACTCP" OR PT_PLATFORM STREQUAL "OT")
    set(TEST_APPS test_fast test_reliable test_chat test_lifecycle test_multi)
    foreach(TEST_APP ${TEST_APPS})
        add_application(${TEST_APP} tests/${TEST_APP}.c tests/status_window.c tests/peertalk_size.r)
        target_include_directories(${TEST_APP} PRIVATE include tests)
        target_compile_definitions(${TEST_APP} PRIVATE ${PLATFORM_DEFINE})
        target_link_libraries(${TEST_APP} PRIVATE peertalk clog)
        if(PT_PLATFORM STREQUAL "OT")
            if(OT_APP_LIB)
                target_link_libraries(${TEST_APP} PRIVATE ${OT_APP_LIB})
            endif()
            if(OT_LIB)
                target_link_libraries(${TEST_APP} PRIVATE ${OT_LIB})
            endif()
            if(OT_INET_LIB)
                target_link_libraries(${TEST_APP} PRIVATE ${OT_INET_LIB})
            endif()
            if(TARGET ot_slm_stubs)
                target_link_libraries(${TEST_APP} PRIVATE ot_slm_stubs)
            endif()
        endif()
    endforeach()

    # Minimal init-only test (PT_Init + PT_Shutdown, isolates SDK crashes)
    add_application(test_init_only tests/test_init_only.c tests/peertalk_size.r)
    target_include_directories(test_init_only PRIVATE include tests)
    target_compile_definitions(test_init_only PRIVATE ${PLATFORM_DEFINE})
    target_link_libraries(test_init_only PRIVATE peertalk clog)
    if(PT_PLATFORM STREQUAL "OT")
        if(OT_APP_LIB)
            target_link_libraries(test_init_only PRIVATE ${OT_APP_LIB})
        endif()
        if(OT_LIB)
            target_link_libraries(test_init_only PRIVATE ${OT_LIB})
        endif()
        if(OT_INET_LIB)
            target_link_libraries(test_init_only PRIVATE ${OT_INET_LIB})
        endif()
        if(TARGET ot_slm_stubs)
            target_link_libraries(test_init_only PRIVATE ot_slm_stubs)
        endif()
    endif()

    # Minimal clog test (no peertalk dependency, no OT/MacTCP linkage)
    add_application(test_clog_minimal tests/test_clog_minimal.c tests/peertalk_size.r)
    target_compile_definitions(test_clog_minimal PRIVATE ${PLATFORM_DEFINE})
    target_link_libraries(test_clog_minimal PRIVATE clog)

endif()
