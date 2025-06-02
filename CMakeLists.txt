cmake_minimum_required(VERSION 3.20)
if(CMAKE_VERSION GREATER_EQUAL 3.30)
    cmake_policy(SET CMP0167 NEW) # prefer cmake configuration provided by the boost package itself
endif()
if(CMAKE_VERSION GREATER_EQUAL 3.31)
    cmake_policy(SET CMP0177 NEW) # normalize all install DESTINATION values except for INCLUDES DESTINATION
endif()

project(workspace
    VERSION 2.0.0
    DESCRIPTION "Future sucessor of hpc-workspace"
    LANGUAGES C CXX
)
# set to version number of the latest release
add_definitions("-DWS_VERSION=\"${workspace_VERSION}\"")

if(IS_DIRECTORY "${CMAKE_SOURCE_DIR}/.git")
    add_definitions(-DIS_GIT_REPOSITORY)
    # git commit hash macro
    execute_process(
        COMMAND git log -1 --format=%h
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE GIT_COMMIT_HASH
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    add_definitions("-DGIT_COMMIT_HASH=\"${GIT_COMMIT_HASH}\"")
endif()

message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Use cmake -DCMAKE_BUILD_TYPE=Release for release build")

add_compile_options(-Wall -Wextra)

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    message(STATUS "Setting Release compiler flags")
    add_compile_options(-O3 -D_GLIBCXX_ASSERTIONS -DNDEBUG)
    add_link_options(-s)
elseif(CMAKE_BUILD_TYPE STREQUAL "Debug")
    message(STATUS "Setting Debug compiler flags")
    add_compile_options(-g3 -fstack-protector-all -fcf-protection=full  -fsanitize=address -D_GLIBCXX_ASSERTIONS)
    add_link_options(-fsanitize=address )
elseif(CMAKE_BUILD_TYPE STREQUAL "Debug-nosan")
    message(STATUS "Setting Debug compiler flags")
    add_compile_options(-g3 -D_GLIBCXX_ASSERTIONS)
    add_link_options(-fsanitize=address )
elseif(CMAKE_BUILD_TYPE STREQUAL "MinSizeRel")
    add_compile_options(-Os -D_GLIBCXX_ASSERTIONS -DNDEBUG)
    add_link_options(-s)
endif()



option(WS_ALLOW_USER_DEBUG "allow users to enable debugging and tracing" FALSE)
if(WS_ALLOW_USER_DEBUG)
    add_definitions("-DWS_ALLOW_USER_DEBUG")
endif()


find_library(LIBCAP NAMES libcap.a libcap.so)
if(LIBCAP)
    message(STATUS "Found libcap (${LIBCAP}). Building with capability support.")
    add_compile_definitions("WS_CAPA")
else()
    message(WARNING "No libcap found on the system. Falling back to SUID.")
    set(LIBCAP "")
endif()


list(APPEND BOOST_COMPONENTS system program_options)
find_package(Boost COMPONENTS ${BOOST_COMPONENTS} REQUIRED)


# find terminfo for "are you human" checker
#find_library(LIBTINFO NAMES libtinfo.a libtinfo.so)
#if(LIBTINFO)
#message(STATUS "Found libtinfo")
#else()
#message(WARNING "No libtinfo found, trying curses alternative ...")
#set(CURSES_NEED_NCURSES TRUE)
#find_package(Curses REQUIRED)
#include_directories(${CURSES_INCLUDE_DIR})
#link_directories(${CURSES_LIBRARY_DIR})
##set(LIBTINFO ${CURSES_LIBRARIES})
#endif()


# for parallel ws_list
add_compile_options(-fopenmp)
add_link_options(-fopenmp)

set(YAML_CPP_BUILD_TESTS OFF CACHE INTERNAL "" FORCE)
add_subdirectory(${workspace_SOURCE_DIR}/external/yaml-cpp)
add_subdirectory(${workspace_SOURCE_DIR}/external/rapidyaml ryml)
add_subdirectory(${workspace_SOURCE_DIR}/external/fmt)
add_subdirectory(${workspace_SOURCE_DIR}/external/GSL)


set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${workspace_BINARY_DIR}/bin)

add_subdirectory(src)


if(BUILD_TESTS)
    add_subdirectory(${workspace_SOURCE_DIR}/external/Catch2)

    include(CTest)
    include(Catch)

    add_subdirectory(tests)
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)


# clang-format: Location of source files
file(GLOB_RECURSE ALL_SOURCE_FILES
     "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
     "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h")

# Custom target to format
add_custom_target(
  clang-format
  COMMAND clang-format
          -i
          -style=file
          ${ALL_SOURCE_FILES}
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  COMMENT "Running clang-format on source files"
)
