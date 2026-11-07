# CTest dashboard script: configure, build, test and collect gcov coverage
# using the "coverage" CMake preset, then submit the results to CDash
# (settings in CTestConfig.cmake).
#
# Usage (from anywhere):
#   ctest -S cmake/dashboard.cmake -V
#
# Options (pass with -D<name>=<value> *before* -S):
#   DASHBOARD_MODEL       Experimental (default), Nightly or Continuous
#   DASHBOARD_FRESH       ON: wipe the build directory first (default OFF)
#   DASHBOARD_RUN_BATS    ON: also run the non-sudo bats CLI tests so that the
#                         coverage includes the tools (default ON if bats found)
#   DASHBOARD_SUBMIT      OFF: do everything except submitting (default ON)
#   DASHBOARD_SITE        CDash site name (default: host name)
#   DASHBOARD_BUILD_NAME  CDash build name (default: <os>-gcc<ver>-coverage[-<branch>])
#   DASHBOARD_JOBS        parallel build/test jobs (default: number of cores)
#
# If the CDash project requires authenticated submissions, export the project
# token as CDASH_AUTH_TOKEN.

cmake_minimum_required(VERSION 3.20)

get_filename_component(CTEST_SOURCE_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(_preset coverage)
# must match binaryDir/generator of the "coverage" preset in CMakePresets.json
set(CTEST_BINARY_DIRECTORY "${CTEST_SOURCE_DIRECTORY}/build/${_preset}")
set(CTEST_CMAKE_GENERATOR "Unix Makefiles")

# ---- options ----------------------------------------------------------------
if(NOT DASHBOARD_MODEL)
    set(DASHBOARD_MODEL Experimental)
endif()
if(NOT DASHBOARD_MODEL MATCHES "^(Experimental|Nightly|Continuous)$")
    message(FATAL_ERROR "DASHBOARD_MODEL must be Experimental, Nightly or Continuous, got '${DASHBOARD_MODEL}'")
endif()
if(NOT DEFINED DASHBOARD_SUBMIT)
    set(DASHBOARD_SUBMIT ON)
endif()
if(NOT DASHBOARD_JOBS)
    cmake_host_system_information(RESULT DASHBOARD_JOBS QUERY NUMBER_OF_LOGICAL_CORES)
endif()

find_program(BATS_EXECUTABLE bats)
if(NOT DEFINED DASHBOARD_RUN_BATS)
    if(BATS_EXECUTABLE)
        set(DASHBOARD_RUN_BATS ON)
    else()
        set(DASHBOARD_RUN_BATS OFF)
    endif()
endif()
if(DASHBOARD_RUN_BATS AND NOT BATS_EXECUTABLE)
    message(FATAL_ERROR "DASHBOARD_RUN_BATS is ON but bats was not found")
endif()

find_program(CTEST_COVERAGE_COMMAND NAMES gcov)
if(NOT CTEST_COVERAGE_COMMAND)
    message(FATAL_ERROR "gcov not found")
endif()

# ---- site and build name ----------------------------------------------------
if(DASHBOARD_SITE)
    set(CTEST_SITE "${DASHBOARD_SITE}")
else()
    site_name(CTEST_SITE)
endif()

if(DASHBOARD_BUILD_NAME)
    set(CTEST_BUILD_NAME "${DASHBOARD_BUILD_NAME}")
else()
    execute_process(COMMAND uname -s OUTPUT_VARIABLE _os OUTPUT_STRIP_TRAILING_WHITESPACE)
    execute_process(COMMAND gcc -dumpfullversion OUTPUT_VARIABLE _gccver OUTPUT_STRIP_TRAILING_WHITESPACE)
    set(CTEST_BUILD_NAME "${_os}-gcc${_gccver}-coverage")
    if(DEFINED ENV{GITHUB_HEAD_REF} AND NOT "$ENV{GITHUB_HEAD_REF}" STREQUAL "")
        set(_branch "$ENV{GITHUB_HEAD_REF}") # pull request source branch
    elseif(DEFINED ENV{GITHUB_REF_NAME})
        set(_branch "$ENV{GITHUB_REF_NAME}")
    else()
        execute_process(COMMAND git rev-parse --abbrev-ref HEAD
            WORKING_DIRECTORY "${CTEST_SOURCE_DIRECTORY}"
            OUTPUT_VARIABLE _branch OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    endif()
    if(_branch)
        string(REPLACE "/" "_" _branch "${_branch}")
        string(APPEND CTEST_BUILD_NAME "-${_branch}")
    endif()
endif()

# ---- steps ------------------------------------------------------------------
# configure through the preset so that flags stay defined in one place
set(CTEST_CONFIGURE_COMMAND "\"${CMAKE_COMMAND}\" -S \"${CTEST_SOURCE_DIRECTORY}\" --preset ${_preset}")

find_program(CTEST_GIT_COMMAND NAMES git)
set(CTEST_UPDATE_COMMAND "${CTEST_GIT_COMMAND}")
set(CTEST_UPDATE_VERSION_ONLY ON) # only report the revision, never touch the checkout

# files not to report coverage for (regexes on absolute paths)
set(CTEST_CUSTOM_COVERAGE_EXCLUDE
    "/fetchdeps/"
    "/external/"
    "/vcpkg"
    "/_deps/"
    "/tests/"
    "/build/"
    "^/usr/"
    "^[^/]" # relative paths: dependencies like Catch2 compiled with relative source paths
)

# report project sources that were never executed (no .gcda) as 0% instead of omitting them;
# patterns are relative to the source directory (CTest prepends it)
set(CTEST_EXTRA_COVERAGE_GLOB
    "src/*.cpp"
    "src/*.h"
)

set(_submit_args)
if(DEFINED ENV{CDASH_AUTH_TOKEN} AND NOT "$ENV{CDASH_AUTH_TOKEN}" STREQUAL "")
    list(APPEND _submit_args HTTPHEADER "Authorization: Bearer $ENV{CDASH_AUTH_TOKEN}")
endif()

if(DASHBOARD_FRESH)
    ctest_empty_binary_directory("${CTEST_BINARY_DIRECTORY}")
endif()

ctest_start(${DASHBOARD_MODEL})
ctest_update(RETURN_VALUE _update_ret)

ctest_configure(RETURN_VALUE _configure_ret)
if(NOT _configure_ret EQUAL 0)
    if(DASHBOARD_SUBMIT)
        ctest_submit(${_submit_args})
    endif()
    message(FATAL_ERROR "configure failed")
endif()

ctest_build(PARALLEL_LEVEL ${DASHBOARD_JOBS} RETURN_VALUE _build_ret NUMBER_ERRORS _build_errors)
if(NOT _build_ret EQUAL 0 OR _build_errors GREATER 0)
    if(DASHBOARD_SUBMIT)
        ctest_submit(${_submit_args})
    endif()
    message(FATAL_ERROR "build failed")
endif()

# reset counters from earlier runs so that coverage reflects this run only
file(GLOB_RECURSE _gcda "${CTEST_BINARY_DIRECTORY}/*.gcda")
if(_gcda)
    file(REMOVE ${_gcda})
endif()

ctest_test(PARALLEL_LEVEL ${DASHBOARD_JOBS} RETURN_VALUE _test_ret)

set(_bats_ret 0)
if(DASHBOARD_RUN_BATS)
    message(STATUS "Running bats CLI tests (non-sudo)")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env PRESET=${_preset}
                "${BATS_EXECUTABLE}" --filter-tags "!sudo" bats/test
        WORKING_DIRECTORY "${CTEST_SOURCE_DIRECTORY}"
        RESULT_VARIABLE _bats_ret
    )
    if(NOT _bats_ret EQUAL 0)
        message(WARNING "bats tests failed (exit code ${_bats_ret})")
    endif()
endif()

# Coverage: instead of ctest_coverage(), which only reports line coverage, run
# "gcov -b -x" and upload the .gcov files as a GcovTar; CDash parses them
# itself including branch coverage. Honors CTEST_CUSTOM_COVERAGE_EXCLUDE and
# CTEST_EXTRA_COVERAGE_GLOB.
include(CTestCoverageCollectGCOV)
set(_gcov_tarball "${CTEST_BINARY_DIRECTORY}/gcov.tar.bz2")
set(_coverage_dir "${CTEST_BINARY_DIRECTORY}/Testing/CoverageInfo")
# drop .gcov and uncovered files of earlier runs
file(REMOVE_RECURSE "${_coverage_dir}" "${CTEST_BINARY_DIRECTORY}/uncovered")
file(REMOVE "${_gcov_tarball}")
ctest_coverage_collect_gcov(
    TARBALL "${_gcov_tarball}"
    TARBALL_COMPRESSION BZIP2
    QUIET
)
if(EXISTS "${_gcov_tarball}")
    # CDash's GcovTar parser counts a branch as untested only if gcov reports
    # "taken 0%"; branches in code that never ran ("never executed") would be
    # counted as tested, giving e.g. 100% branch coverage for unexecuted files.
    # Rewrite them and repack the tarball from the module's file list.
    file(STRINGS "${_coverage_dir}/coverage_file_list.txt" _tar_files)
    foreach(_f IN LISTS _tar_files)
        if(_f MATCHES "\\.gcov$")
            file(READ "${CTEST_BINARY_DIRECTORY}/${_f}" _content)
            string(REGEX REPLACE "(\nbranch +[0-9]+) never executed" "\\1 taken 0%" _content "${_content}")
            file(WRITE "${CTEST_BINARY_DIRECTORY}/${_f}" "${_content}")
        endif()
    endforeach()
    file(REMOVE "${_gcov_tarball}")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E tar cjf "${_gcov_tarball}"
                "--mtime=1970-01-01 0:0:0 UTC" --format=gnutar
                "--files-from=${_coverage_dir}/coverage_file_list.txt"
        WORKING_DIRECTORY "${CTEST_BINARY_DIRECTORY}"
        RESULT_VARIABLE _tar_ret
    )
    if(NOT _tar_ret EQUAL 0)
        message(FATAL_ERROR "repacking ${_gcov_tarball} failed")
    endif()
else()
    message(WARNING "no coverage data collected")
endif()

if(DASHBOARD_SUBMIT)
    ctest_submit(${_submit_args} RETURN_VALUE _submit_ret)
    if(NOT _submit_ret EQUAL 0)
        message(FATAL_ERROR "submission to CDash failed")
    endif()
    if(EXISTS "${_gcov_tarball}")
        ctest_submit(${_submit_args}
            CDASH_UPLOAD "${_gcov_tarball}"
            CDASH_UPLOAD_TYPE GcovTar
            RETURN_VALUE _submit_ret)
        if(NOT _submit_ret EQUAL 0)
            message(FATAL_ERROR "coverage upload to CDash failed")
        endif()
    endif()
endif()

if(NOT _test_ret EQUAL 0 OR NOT _bats_ret EQUAL 0)
    message(FATAL_ERROR "tests failed")
endif()
