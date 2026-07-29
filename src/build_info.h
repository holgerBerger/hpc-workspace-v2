#pragma once

/*
 *  hpc-workspace-v2
 *
 *  build_info.h
 *
 *  - helper functions
 *
 *  c++ version of workspace utility
 *  a workspace is a temporary directory created in behalf of a user with a limited lifetime.
 *
 *  (c) Holger Berger 2021,2023,2024,2025,2026
 *  (c) Christoph Niethammer 2025
 *
 *  hpc-workspace-v2 is based on workspace by Holger Berger, Thomas Beisel and Martin Hecht
 *
 *  hpc-workspace-v2 is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  hpc-workspace-v2 is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with workspace-ng  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "BS_thread_pool.hpp"
#include "c4/yml/version.hpp"
#include "spdlog/version.h"
#include <curl/curl.h>
#include <fmt/base.h>
#include <fmt/format.h>

#include "caps.h"

extern Cap caps;

namespace utils {

// forward declaration of the function implemented in dbv1.cpp
std::string getDBYamlReader();

/// helper to print build flags
inline void printBuildFlags() {
    bool capa = false;

#ifdef WS_CAPA
    capa = true;
#endif
    fmt::println("Build flags: WS_CAPA={}", capa);
    fmt::println("Runtime flags: isusermode={}, issetuid={}, hascaps={}", caps.isUserMode(), caps.isSetuid(),
                 caps.hasCaps());
}

/**
 * helper to print basic version information
 *
 * @param[in] program_name  use program_name for output
 */
inline void printVersion(std::string program_name) {
#ifdef IS_GIT_REPOSITORY
    fmt::println("{} build from git commit hash {} on top of release {}", program_name, GIT_COMMIT_HASH, WS_VERSION);
#else
    fmt::println("{} version {}", program_name, WS_VERSION);
#endif
    fmt::println("DB YAML reader: {}", getDBYamlReader());
    fmt::println("Dependency versions:");
    fmt::println("  yaml-cpp: 0.9.0");
    fmt::println("  rapidyaml (ryml): {}", RYML_VERSION);
    fmt::println("  fmt: {}.{}.{}", FMT_VERSION / 10000, (FMT_VERSION / 100) % 100, FMT_VERSION % 100);
    fmt::println("  spdlog: {}.{}.{}", SPDLOG_VER_MAJOR, SPDLOG_VER_MINOR, SPDLOG_VER_PATCH);
    fmt::println("  Microsoft GSL: 4.2.2");
    fmt::println("  libcurl: {}", LIBCURL_VERSION);
    fmt::println("  bshoshany-thread-pool: {}.{}.{}", BS_THREAD_POOL_VERSION_MAJOR, BS_THREAD_POOL_VERSION_MINOR,
                 BS_THREAD_POOL_VERSION_PATCH);
}

inline std::string getVersion() {
    std::string db_yaml = getDBYamlReader();
    std::string fmt_ver = fmt::format("{}.{}.{}", FMT_VERSION / 10000, (FMT_VERSION / 100) % 100, FMT_VERSION % 100);
    std::string spdlog_ver = fmt::format("{}.{}.{}", SPDLOG_VER_MAJOR, SPDLOG_VER_MINOR, SPDLOG_VER_PATCH);
    std::string ryml_ver = RYML_VERSION;
    std::string curl_ver = LIBCURL_VERSION;
    std::string tp_ver = fmt::format("{}.{}.{}", BS_THREAD_POOL_VERSION_MAJOR, BS_THREAD_POOL_VERSION_MINOR,
                                     BS_THREAD_POOL_VERSION_PATCH);

#ifdef IS_GIT_REPOSITORY
    return fmt::format("build from git commit hash {} on top of release {} (DB reader: {}, yaml-cpp: 0.9.0, ryaml: {}, "
                       "fmt: {}, spdlog: {}, gsl: 4.2.2, curl: {}, thread-pool: {})",
                       GIT_COMMIT_HASH, WS_VERSION, db_yaml, ryml_ver, fmt_ver, spdlog_ver, curl_ver, tp_ver);
#else
    return fmt::format("version {} (DB reader: {}, yaml-cpp: 0.9.0, ryaml: {}, fmt: {}, spdlog: {}, gsl: 4.2.2, curl: "
                       "{}, thread-pool: {})",
                       WS_VERSION, db_yaml, ryml_ver, fmt_ver, spdlog_ver, curl_ver, tp_ver);
#endif
}

} // namespace utils
