cmake_minimum_required(VERSION 3.20)

# Handle newer CMake policies gracefully
if(CMAKE_VERSION GREATER_EQUAL 3.30)
    cmake_policy(SET CMP0167 NEW) # Prefer CMake configuration provided by the boost package itself
endif()

if(CMAKE_VERSION GREATER_EQUAL 3.31)
    cmake_policy(SET CMP0177 NEW) # Normalize all install DESTINATION values
endif()

project(workspace
    VERSION 2.0.0
    DESCRIPTION "Future successor of hpc-workspace"
    LANGUAGES C CXX
)

include(GNUInstallDirs)

if(NOT DEFINED CMAKE_INSTALL_MAN1DIR)
    set(CMAKE_INSTALL_MAN1DIR "${CMAKE_INSTALL_MANDIR}/man1")
endif()
if(NOT DEFINED CMAKE_INSTALL_MAN8DIR)
    set(CMAKE_INSTALL_MAN8DIR "${CMAKE_INSTALL_MANDIR}/man8")
endif()

# Set version macro
add_compile_definitions(WS_VERSION="${workspace_VERSION}")

# Git commit hash if in repository
if(IS_DIRECTORY "${CMAKE_SOURCE_DIR}/.git")
    add_compile_definitions(IS_GIT_REPOSITORY=1)
    execute_process(
        COMMAND git log -1 --format=%h
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE GIT_COMMIT_HASH
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(GIT_COMMIT_HASH)
        add_compile_definitions(GIT_COMMIT_HASH="${GIT_COMMIT_HASH}")
        message(STATUS "Git commit: ${GIT_COMMIT_HASH}")
    endif()
endif()

message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")


# Capability support option
set(WS_USE_CAPABILITIES "AUTO" CACHE STRING "Use capabilities for privilege elevation. AUTO=auto-detect, ON=require libcap, OFF=force SUID only")
set_property(CACHE WS_USE_CAPABILITIES PROPERTY STRINGS AUTO ON OFF)

# Setuid UID and GID options
set(WS_SETUID_UID "" CACHE STRING "UID for setuid binaries (empty = root)")
set(WS_SETUID_GID "" CACHE STRING "GID for setuid binaries (empty = root)")

if(WS_USE_CAPABILITIES STREQUAL "OFF")
    message(STATUS "Capabilities disabled by user. Building with SUID only.")
    set(LIBCAP "")
else()
    find_library(LIBCAP NAMES cap libcap libcap.a libcap.so)
    if(NOT LIBCAP)
        if(WS_USE_CAPABILITIES STREQUAL "ON")
            message(FATAL_ERROR "libcap not found and WS_USE_CAPABILITIES is ON. Install libcap-devel/libcap2-dev or set WS_USE_CAPABILITIES to AUTO or OFF.")
        else()
            message(WARNING "libcap not found. Falling back to SUID.")
        endif()
    else()
        message(STATUS "Found libcap: ${LIBCAP}. Building with capability support.")
    endif()
endif()

if(LIBCAP)
    add_compile_definitions(WS_CAPA=1)
endif()

# Find Boost with program_options
find_package(Boost REQUIRED COMPONENTS program_options OPTIONAL_COMPONENTS system)

find_package(CURL REQUIRED)

# spdlog requires external fmt before it
set(SPDLOG_FMT_EXTERNAL ON CACHE BOOL "" FORCE)

# Use vcpkg for all dependencies (skips FetchContent)
set(BUILD_WITH_VCPKG OFF CACHE BOOL "Build all dependencies via vcpkg instead of FetchContent")
mark_as_advanced(BUILD_WITH_VCPKG)

if(NOT BUILD_WITH_VCPKG)
    message(STATUS "Using FetchContent for dependencies")

    include(FetchContent)

    # Speed up subsequent configures: skip re-fetching if deps are already present
    set(FETCHCONTENT_UPDATES_DISCONNECTED ON)

    # Suppress subproject installations
    set(GLAZE_INSTALL OFF CACHE BOOL "" FORCE)
    set(FMT_INSTALL OFF CACHE BOOL "" FORCE)
    set(MS_GSL_INSTALL OFF CACHE BOOL "" FORCE)
    set(SPDLIB_INSTALL OFF CACHE BOOL "" FORCE)

    FetchContent_Declare(glaze
        GIT_REPOSITORY https://github.com/stephenberry/glaze.git
        GIT_TAG v7.6.0
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(glaze)

    FetchContent_Declare(fmt
        GIT_REPOSITORY https://github.com/fmtlib/fmt.git
        GIT_TAG 12.1.0
    )
    FetchContent_MakeAvailable(fmt)

    FetchContent_Declare(GSL
        GIT_REPOSITORY https://github.com/microsoft/GSL.git
        GIT_TAG v4.2.1
    )
    FetchContent_MakeAvailable(GSL)

    FetchContent_Declare(spdlog
        GIT_REPOSITORY https://github.com/gabime/spdlog.git
        GIT_TAG v1.15.1
    )
    FetchContent_MakeAvailable(spdlog)

    FetchContent_Declare(thread_pool
        GIT_REPOSITORY https://github.com/bshoshany/thread-pool.git
        GIT_TAG v5.1.0
    )
    FetchContent_MakeAvailable(thread_pool)

    if(BUILD_TESTS)
        FetchContent_Declare(Catch2
            GIT_REPOSITORY https://github.com/catchorg/Catch2.git
            GIT_TAG v3.7.1
        )
        FetchContent_MakeAvailable(Catch2)
    endif()

else()
    message(STATUS "Building with vcpkg — FetchContent is disabled")
    # vcpkg must be initialized before this project (via CMAKE_TOOLCHAIN_FILE)
    if(NOT CMAKE_TOOLCHAIN_FILE MATCHES "vcpkg")
        message(FATAL_ERROR
            "BUILD_WITH_VCPKG is ON but CMAKE_TOOLCHAIN_FILE does not point to vcpkg.cmake.\n"
            "  Run: bash cmake/vcpkg-bootstrap.sh")
    endif()
endif()

# Set runtime output directory
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${workspace_BINARY_DIR}/bin)

# Main source
add_subdirectory(src)

# Tests (optional)
if(BUILD_TESTS)
    if(BUILD_WITH_VCPKG)
        find_package(Catch2 CONFIG REQUIRED)
        # Find the Catch.cmake module that comes with Catch2 vcpkg package
        get_target_property(_CATCH2_DIR Catch2::Catch2WithMain IMPORTED_LOCATION_DEBUG)
        if(NOT _CATCH2_DIR)
            get_target_property(_CATCH2_DIR Catch2::Catch2WithMain INTERFACE_INCLUDE_DIRECTORIES)
        endif()
        if(_CATCH2_DIR)
            list(APPEND CMAKE_MODULE_PATH "${_CATCH2_DIR}")
        endif()
        list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_BINARY_DIR}/vcpkg_installed/${CMAKE_VCPKG_TRIPLET}/share/catch2")
        include(Catch)
    else()
        include(Catch)
    endif()
    enable_testing()
    add_subdirectory(tests)
endif()

# Formatting target
file(GLOB_RECURSE ALL_SOURCE_FILES
     "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
     "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
     "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp"
     "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.h")

add_custom_target(
  clang-format
  COMMAND clang-format -i -style=file ${ALL_SOURCE_FILES}
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  COMMENT "Running clang-format on source files"
)

# Install manpages
install(DIRECTORY man/man1/ DESTINATION ${CMAKE_INSTALL_MAN1DIR} FILES_MATCHING PATTERN "*.1")
install(DIRECTORY man/man8/ DESTINATION ${CMAKE_INSTALL_MAN8DIR} FILES_MATCHING PATTERN "*.8")

# Install shell scripts
install(FILES
    ${CMAKE_SOURCE_DIR}/bin/ws_extend
    ${CMAKE_SOURCE_DIR}/bin/ws_share
    DESTINATION ${CMAKE_INSTALL_BINDIR}
    PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)

message(STATUS "Project ${PROJECT_NAME} v${PROJECT_VERSION} configured successfully")
