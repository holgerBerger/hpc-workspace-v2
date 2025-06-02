/*
 *  hpc-workspace-v2
 *
 *  ws_prepare
 *
 *  - tool to prepare workspaces according to a configuration
 *    Does not fix bad permissions of structure exists!
 *    only for initial creation, useful e.g. for testing
 *
 *  c++ version of workspace utility
 *  a workspace is a temporary directory created in behalf of a user with a limited lifetime.
 *
 *  (c) Holger Berger 2021,2023,2024,2025
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

#include <filesystem>
#include <iostream> // for program_options  FIXME:

#include "config.h"
#include <boost/program_options.hpp>

#include "build_info.h"
#include "db.h"
#include "fmt/base.h"
#include "fmt/ranges.h" // IWYU pragma: keep
#include "user.h"

#include "caps.h"

#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>

// init caps here, when euid!=uid
Cap caps{};

namespace cppfs = std::filesystem;
namespace po = boost::program_options;
using namespace std;

bool debugflag = false;
bool traceflag = false;

int main(int argc, char** argv) {

    // options and flags
    string configfile;

    if (!user::isRoot()) {
        fmt::println(stderr, "Error   : you are not root!");
        exit(1);
    }

    po::variables_map opts;

    // locals settings to prevent strange effects
    utils::setCLocal();

    // define options
    po::options_description cmd_options("\nOptions");
    cmd_options.add_options()("help,h", "produce help message")("version,V", "show version")(
        "config", po::value<string>(&configfile), "config file");

    po::options_description secret_options("Secret");
    secret_options.add_options()("debug", "show debugging information")("trace", "show tracing information");

    po::options_description all_options;
    all_options.add(cmd_options).add(secret_options);

    // parse commandline
    try {
        po::store(po::command_line_parser(argc, argv).options(all_options).run(), opts);
        po::notify(opts);
    } catch (...) {
        fmt::print("Usage: {} [options] [pattern]\n", argv[0]);
        cout << cmd_options << endl; // FIXME: can not be printed with fmt??
        exit(1);
    }

    debugflag = opts.count("debug");
    traceflag = opts.count("trace");

    // handle options exiting here

    if (opts.count("help")) {
        fmt::print("Usage: {} [options] [pattern]\n", argv[0]);
        cout << cmd_options << endl; // FIXME: can not be printed with fmt??
        exit(0);
    }

    if (opts.count("version")) {
#ifdef IS_GIT_REPOSITORY
        fmt::println("workspace build from git commit hash {} on top of release {}", GIT_COMMIT_HASH, WS_VERSION);
#else
        fmt::println("workspace version {}", WS_VERSION);
#endif
        utils::printBuildFlags();
        exit(0);
    }

    // read config
    //   user can change this if no setuid installation OR if root
    auto configfilestoread = std::vector<cppfs::path>{"/etc/ws.d", "/etc/ws.conf"};
    if (configfile != "") {
        if (user::isRoot() || caps.isUserMode()) {
            configfilestoread = {configfile};
        } else {
            fmt::print(stderr, "Warning: ignored config file options!\n");
        }
    }

    auto config = Config(configfilestoread);
    if (!config.isValid()) {
        fmt::println(stderr, "Error  : No valid config file found!");
        exit(-2);
    }

    // end of boilerplate

    // loop over all workspaces
    for (auto const& fs : config.Filesystems()) {
        fmt::println("workspace: {}", fs);

        // create DB directory
        auto DBdir = cppfs::path(config.getFsConfig(fs).database);
        fmt::println("  DB directory: {}", DBdir.string());
        if (!cppfs::exists(DBdir)) {
            try {
                cppfs::create_directory(DBdir);
            } catch (cppfs::filesystem_error const& e) {
                fmt::println(stderr, e.what());
            }
            auto ret = chmod(DBdir.c_str(), 0755);
            if (ret != 0)
                perror(NULL);
            ret = chown(DBdir.c_str(), config.dbuid(), config.dbgid());
            if (ret != 0)
                perror(NULL);
        } else {
            fmt::println("    existed already, did not check/change permissions!");
        }

        // create DB deleted
        auto DBdeleted = cppfs::path(config.getFsConfig(fs).database) / cppfs::path(config.getFsConfig(fs).deletedPath);
        fmt::println("  DB deleted directory: {}", DBdeleted.string());
        if (!cppfs::exists(DBdeleted)) {
            try {
                cppfs::create_directory(DBdeleted);
            } catch (cppfs::filesystem_error const& e) {
                fmt::println(stderr, e.what());
            }
            auto ret = chmod(DBdeleted.c_str(), 0755);
            if (ret != 0)
                perror(NULL);
            ret = chown(DBdeleted.c_str(), config.dbuid(), config.dbgid());
            if (ret != 0)
                perror(NULL);
        } else {
            fmt::println("    existed already, did not check/change permissions!");
        }

        // make sure magic is written
        if (!cppfs::exists(DBdir / ".ws_db_magic")) {
            utils::writeFile(DBdir / ".ws_db_magic", fs + "\n");
        }

        // workspace directories
        for (auto const& sp : config.getFsConfig(fs).spaces) {
            // workspace itself
            fmt::println("  workspace directory: {}", sp);
            if (!cppfs::exists(sp)) {
                try {
                    cppfs::create_directories(sp);
                } catch (cppfs::filesystem_error const& e) {
                    fmt::println(stderr, e.what());
                }
                auto ret = chmod(sp.c_str(), 0755);
                if (ret != 0)
                    perror(NULL);
                ret = chown(sp.c_str(), config.dbuid(), config.dbgid());
                if (ret != 0)
                    perror(NULL);
            } else {
                fmt::println("    existed already, did not check/change permissions!");
            }
            // deleted
            auto WSdeleted = cppfs::path(sp) / cppfs::path(config.getFsConfig(fs).deletedPath);
            fmt::println("  deleted workspace directory: {}", WSdeleted.string());
            if (!cppfs::exists(WSdeleted)) {
                try {
                    cppfs::create_directories(WSdeleted);
                } catch (cppfs::filesystem_error const& e) {
                    fmt::println(stderr, e.what());
                }
                auto ret = chmod(WSdeleted.c_str(), 0755);
                if (ret != 0)
                    perror(NULL);
                ret = chown(WSdeleted.c_str(), config.dbuid(), config.dbgid());
                if (ret != 0)
                    perror(NULL);
            } else {
                fmt::println("    existed already, did not check/change permissions!");
            }
        }
    }
}
