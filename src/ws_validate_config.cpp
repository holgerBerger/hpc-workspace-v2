/*
 *  hpc-workspace-v2
 *
 *  ws_validate_config
 *
 *  -
 *
 *  c++ version of workspace utility
 *  a workspace is a temporary directory created in behalf of a user with a limited lifetime.
 *
 *  (c) Holger Berger 2021,2023,2024,2025
 *  (c) Maria Hampel 2025
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

#ifdef WS_RAPIDYAML_CONFIG
    #define RYML_USE_ASSERT 0
    #include "c4/format.hpp"
    #include "c4/std/std.hpp"
    #include "ryml.hpp"
    #include "ryml_std.hpp"
#else
    #include "yaml-cpp/yaml.h"
#endif

#include "fmt/base.h"
#include "fmt/ostream.h"
#include "fmt/ranges.h" // IWYU pragma: keep
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "caps.h"
#include "config.h"
#include "utils.h"
#include "ws.h"

#include "spdlog/spdlog.h"

#include <boost/program_options.hpp>
namespace po = boost::program_options;
using namespace std;
namespace cppfs = std::filesystem;

// global variables
bool debugflag = false;
bool traceflag = false;
int debuglevel = 0;

// init caps here, when euid!=uid
Cap caps{};

cppfs::perms perm0755 = cppfs::perms::owner_read | cppfs::perms::owner_write | cppfs::perms::owner_exec |
                        cppfs::perms::group_read | cppfs::perms::group_exec | cppfs::perms::others_read |
                        cppfs::perms::others_exec;

cppfs::perms perm0644 =
    cppfs::perms::owner_read | cppfs::perms::owner_write | cppfs::perms::group_read | cppfs::perms::others_read;

int main(int argc, char** argv) {
    std::string configfile = "";
    // locals settings to prevent strange effects
    utils::setCLocal();

    // set custom logging format
    utils::setupLogging(string(argv[0]));

    po::variables_map opt;
    // define all options
    po::options_description cmd_options("\nOptions");

    // clang-format off
    cmd_options.add_options()
        ("help,h", "produce help message")
        ("config", po::value<string>(&configfile), "config file");
    // clang-format on

    // define options without names
    po::positional_options_description p;
    p.add("filename", 1);

    // parse commandline
    try {
        po::store(po::command_line_parser(argc, argv).options(cmd_options).positional(p).run(), opt);
        po::notify(opt);
    } catch (...) {
        cerr << "Usage: " << argv[0] << " [filename]" << endl;
        cerr << cmd_options << "\n";
        exit(1);
    }

    if (opt.count("help")) {
        cerr << "Usage: " << argv[0] << " [filename]" << endl;
        cerr << cmd_options << "\n";
        cerr << "this command is used to validate a config file" << endl;
        exit(0);
    }

    auto configfilestoread = std::vector<cppfs::path>{"/etc/ws.d", "/etc/ws.conf"};
    if (configfile != "") {
        if (user::isRoot() || caps.isUserMode()) {
            configfilestoread = {configfile};
        } else {
            spdlog::warn("ignored config file options!");
        }
    }

    auto config = Config(configfilestoread);

    spdlog::info("validating config from {}", configfile);

    // see if there are workspaces/filesystems defined
    auto wsnames = config.Filesystems();

    if (wsnames.empty()) {
        spdlog::error("No Workspaces defined");
        exit(1);
    }

    auto clustername = config.clustername();
    if (clustername != "") {
        fmt::println("clustername: {}", clustername);
    } else {
        spdlog::error("No clustername definied, please add \"clustername: <name>\" clause to toplevel");
        exit(1);
    }

    auto smtphost = config.smtphost();
    if (smtphost != "") {
        fmt::println("smtphost: {}", smtphost);
    } else {
        spdlog::warn("No smtphost found, beware: no reminder and error mails can be send. Please add \"smtphost: "
                     "<name>\" clause to toplevel");
    }

    auto mail_from = config.mailfrom();
    if (mail_from != "") {
        fmt::println("mail_from: {}", mail_from);
    } else {
        spdlog::warn("No mail_from found, beware: no reminder and error mails can be send. Please add \"mail_from: "
                     "<mail>\" clause to toplevel");
    }

    // default/default_workspace
    std::string defaultws = config.defaultworkspace();
    if (defaultws != "") {
        fmt::println("default: {}", defaultws);
    } else {
        spdlog::error("No default workspace found, please add \"default: <name>\" clause to toplevel");
        exit(1);
    }

    // try if defaultws is actually in the ws
    if (!(std::find(wsnames.begin(), wsnames.end(), defaultws) != wsnames.end())) {
        spdlog::error("default workspace is not defined as workspace in the file");
        exit(1);
    }

    int duration = config.maxduration();
    fmt::println("maxduration: {}", duration);

    auto durationdefault = config.durationdefault();
    fmt::println("durationdefault: {}", durationdefault);

    auto reminderdefault = config.reminderdefault();
    fmt::println("reminderdefault: {}", reminderdefault);

    auto maxextensions = config.maxextensions();
    fmt::println("maxextensions: {}", maxextensions);

    auto dbuid = config.dbuid();
    fmt::println("dbuid: {}", dbuid);

    auto dbgid = config.dbgid();
    fmt::println("dbgid: {}", dbgid);

    auto admins = config.admins();
    if (!admins.empty()) {
        fmt::println("admins: {}", admins);
    } else {
        spdlog::error("No admins found, please add \"admins:: <[List]>\" clause to toplevel");
        exit(1);
    }

    auto adminmail = config.adminmail();
    if (!adminmail.empty()) {
        fmt::println("adminmail: {}", adminmail);
    } else {
        spdlog::error("No adminmail found, please add \"adminmail: <[List]>\" clause to toplevel");
        exit(1);
    }

    auto expirerlogpath = config.expirerlogpath();
    if (expirerlogpath != "") {
        fmt::println("expirerlogpath: {}", expirerlogpath);
        if (!cppfs::exists(expirerlogpath)) {
            spdlog::warn("expirer directory {} does not exist", expirerlogpath);
        }
    } else {
        fmt::println("No expirerlogpath found, continuing");
    }

    for (auto name : wsnames) {
        auto ws = config.getFsConfig(name);

        std::string wsname = ws.name;
        if (wsname != "") {
            spdlog::info("checking config for filesystem {}", wsname);
        } else {
            spdlog::error("Invalid Filesystem Name");
            exit(1);
        }

        std::string deleted = ws.deletedPath;
        if (deleted != "") {
            fmt::println("    deleted: {}", deleted);
        } else {
            spdlog::error("No deleted directory found for filesystem {}, please add \"deleted: <dir>\" clause "
                          "to workspace",
                          wsname);
            exit(1);
        }

        auto spaces = ws.spaces;
        if (!spaces.empty()) {
            fmt::println("    spaces: {}", spaces);
            for (auto space : spaces) {
                cppfs::path deletedpath = space + "/" + deleted;

                if (!cppfs::exists(space)) {
                    spdlog::warn("spaces directory {} does not exist", space);
                }
                if (!cppfs::exists(deletedpath)) {
                    spdlog::warn("spaces directory {} does not contain deleted folder {}", space, deleted);
                }

                cppfs::perms perms;
                perms = cppfs::status(space).permissions();
                if (perms != perm0755) {
                    spdlog::warn("spaces path {} has incorrect permissions", space);
                }
                perms = cppfs::status(deletedpath).permissions();
                if (perms != perm0755) {
                    spdlog::warn("deleted directory {} in space {} has incorrect permissions", deleted, space);
                }
            }
        } else {
            spdlog::error("No spaces found for filesystem {}, please add \"spaces: <[List]>\"", wsname);
            exit(1);
        }

        auto spaceselection = ws.spaceselection;
        if (spaceselection != "") {
            fmt::println("    spaceselection: {}", spaceselection);
        } else {
            fmt::println("    No spaceselection found, defaults to 'random', continuing");
        }

        auto database = ws.database;

        if (database != "") {
            cppfs::path wsdbmagicpath = database + "/.ws_db_magic";
            cppfs::path deletedpath = database + "/" + deleted;

            fmt::println("    workspace database directory: {}", database);
            if (!cppfs::exists(database)) {
                spdlog::warn("database directory {} does not exist", database);
            }
            if (!cppfs::exists(wsdbmagicpath)) {
                spdlog::warn("database directory {} does not contain .ws_db_magic!", database);
            }
            if (!cppfs::exists(deletedpath)) {
                spdlog::warn("database directory {} does not contain deleted folder {}", database, deleted);
            }

            // Check if permissions are correct
            cppfs::perms perms;
            perms = cppfs::status(database).permissions();
            if (perms != perm0755) {
                spdlog::warn("database path {} has incorrect permissions", database);
            }
            perms = cppfs::status(wsdbmagicpath).permissions();
            if (perms != perm0644) {
                spdlog::warn(".ws_db_magic in database directory {} has incorrect permissions", database);
            }
            perms = cppfs::status(deletedpath).permissions();
            if (perms != perm0755) {
                spdlog::warn("deleted directory {} in database {} has incorrect permissions", deleted, database);
            }
        } else {
            spdlog::error("No database location define for filesystem {}, please add \"database: <dir>\" "
                          "clause to workspace",
                          wsname);
            exit(1);
        }

        auto groupdefault = ws.groupdefault;
        if (!groupdefault.empty()) {
            fmt::println("    groupdefault: {}", groupdefault);
        } else {
            fmt::println("    No groupdefault found, continuing");
        }

        auto userdefault = ws.userdefault;
        if (!userdefault.empty()) {
            fmt::println("    userdefault: {}", userdefault);
        } else {
            fmt::println("    No userdefault found, continuing");
        }

        auto user_acl = ws.user_acl;
        if (!user_acl.empty()) {
            fmt::println("    user_acl: {}", user_acl);
        } else {
            fmt::println("    No user_acl found, continuing");
        }

        auto group_acl = ws.group_acl;
        if (!group_acl.empty()) {
            fmt::println("    group_acl: {}", group_acl);
        } else {
            fmt::println("    No group_acl found, continuing");
        }

        auto keeptime = ws.keeptime;
        fmt::println("    keeptime: {}", keeptime);

        int duration = ws.maxduration;
        fmt::println("    maxduration: {}", duration);

        auto maxextensions = ws.maxextensions;
        fmt::println("    maxextensions: {}", maxextensions);

        auto allocatable = ws.allocatable;
        fmt::println("    allocatable: {}", allocatable);

        auto extendable = ws.extendable;
        fmt::println("    extendable: {}", extendable);

        auto restorable = ws.restorable;
        fmt::println("    restorable: {}", restorable);
    }
    spdlog::info("config is valid!");
}
