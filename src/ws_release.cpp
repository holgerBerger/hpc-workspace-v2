/*
 *  hpc-workspace-v2
 *
 *  ws_release
 *
 *  - tool to release workspaces
 *
 *  c++ version of workspace utility
 *  a workspace is a temporary directory created in behalf of a user with a limited lifetime.
 *
 *  (c) Holger Berger 2021,2023,2024,2025
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

#include "fmt/base.h"
#include "fmt/ostream.h"
#include "fmt/ranges.h" // IWYU pragma: keep
#include <memory>
#include <string>

#include <regex> // buggy in redhat 7

#include <syslog.h>
#include <time.h>
#include <unistd.h>

#include "build_info.h"
#include "caps.h"
#include "config.h"
#include "user.h"
#include "utils.h"
#include "ws.h"

#include "spdlog/spdlog.h"

#include <boost/program_options.hpp>
namespace po = boost::program_options;
using namespace std;

#include <filesystem>
namespace cppfs = std::filesystem;

// global variables

bool debugflag = false;
bool traceflag = false;
int debuglevel = 0;

// init caps here, when euid!=uid
Cap caps{};

// helper for fmt::
template <> struct fmt::formatter<po::options_description> : ostream_formatter {};

/*
 *  parse the commandline and see if all required arguments are passed, and check the workspace name for
 *  bad characters
 */
void commandline(po::variables_map& opt, string& name, string& filesystem, string& user, bool& deletedata, int argc,
                 char** argv, std::string& configfile) {
    // define all options

    po::options_description cmd_options("\nOptions");
    // clang-format off
    cmd_options.add_options()
        ("help,h", "produce help message")
        ("version,V", "show version")
        ("name,n", po::value<string>(&name), "workspace name")
        ("filesystem,F", po::value<string>(&filesystem), "filesystem")
        ("username,u", po::value<string>(&user), "username")
        ("config", po::value<string>(&configfile), "config file")
        ("delete-data", "delete all data, workspace can NOT BE RECOVERED!");
    // clang-format on

    po::options_description secret_options("Secret");
    secret_options.add_options()("debug", "show debugging information")("trace", "show calling information");

    // define options without names
    po::positional_options_description p;
    p.add("name", 1);

    po::options_description all_options;
    all_options.add(cmd_options).add(secret_options);

    // parse commandline
    try {
        po::store(po::command_line_parser(argc, argv).options(all_options).positional(p).run(), opt);
        po::notify(opt);
    } catch (...) {
        fmt::println(stderr, "Usage: {} [options] workspace_name\n", argv[0]);
        fmt::println(stderr, "{}", cmd_options);
        exit(1);
    }

    // see whats up

    if (opt.count("help")) {
        fmt::println(stderr, "Usage: {} [options] workspace_name\n", argv[0]);
        fmt::println(stderr, "{}", cmd_options);
        exit(0);
    }

    if (opt.count("version")) {
        utils::printVersion("ws_release");
        utils::printBuildFlags();
        exit(0);
    }

    // this allows user to extend foreign workspaces
    if (opt.count("username") && !(opt.count("extension") || getuid() == 0)) {
        spdlog::info("Ignoring username option.");
        user = "";
    }

    if (opt.count("name")) {
        // cout << " name: " << name << "\n";
    } else {
        fmt::println(stderr, "Usage: {} [options] workspace_name\n", argv[0]);
        fmt::println(stderr, "{}", cmd_options);
        exit(1);
    }

    deletedata = opt.count("delete-data");

    // globalflags
    debugflag = opt.count("debug");
    traceflag = opt.count("trace");

    // validate workspace name against nasty characters
    // TODO: remove regexp dependency
    static const regex e(ws::workspace_name_regex);
    if (!regex_match(name, e)) {
        spdlog::error("Illegal workspace name, use ASCII characters and numbers, '-','.' and '_' only!");
        exit(1);
    }
}

/*
 *  validate parameters vs config file
 *
 *  return true if ok and false if not
 */
bool validateFs(const Config& config, const po::variables_map& opt, const std::string username) {
    if (traceflag)
        spdlog::trace("validateFsAndGroup(username={})", username);

    // auto groupnames=getgroupnames(username); // FIXME:  use getGrouplist ?
    auto groupnames = user::getGrouplist();

    // if the user specifies a filesystem, he must be allowed to use it
    if (opt.count("filesystem")) {
        auto validfs = config.validFilesystems(username, groupnames, ws::RELEASE);
        if (!canFind(validfs, opt["filesystem"].as<string>()) && getuid() != 0) {
            spdlog::error("You are not allowed to use the specified filesystem!");
            return false;
        }
    }

    return true;
}

/*
 *  release the workspace
 *  file accesses and config access are hidden in DB and config handling
 */
bool release(const Config& config, const po::variables_map& opt, string filesystem, const string name,
             string user_option, const bool deletedata) {
    if (traceflag)
        spdlog::trace("release({}, {}, {}, {})", filesystem, name, user_option, deletedata);

    std::string username = user::getUsername(); // current user

    // get valid filesystems to bail out if there is none
    auto valid_filesystems = config.validFilesystems(user::getUsername(), user::getGrouplist(), ws::RELEASE);

    if (valid_filesystems.size() == 0) {
        spdlog::error("no valid filesystems in configuration, can not release");
        return false; // FIXME: bad for testing
    }

    // validate filesystem and group given on command line
    if (!validateFs(config, opt, user_option)) {
        spdlog::error("aborting, no valid filesystem given.");
        return false;
    }

    // if no filesystem provided, get valid filesystems from config, ordered: userdefault, groupdefault, globaldefault,
    // others
    vector<string> searchlist;
    if (opt.count("filesystem")) {
        searchlist.push_back(opt["filesystem"].as<string>());
    } else {
        searchlist =
            config.validFilesystems(user::getUsername(), user::getGrouplist(),
                                    ws::RELEASE); // FIXME: getUsername or user_option? getGrouplist uses current uid
    }

    //
    // now search for workspace in filesystem(s) and see if it exists or create it
    //

    bool ws_exists = false;

    std::unique_ptr<DBEntry> dbentry;
    std::string dbid;

    // loop over valid workspaces, and see if the workspace exists

    std::unique_ptr<Database> db;
    std::vector<std::unique_ptr<DBEntry>> entrylist;

    for (std::string cfilesystem : searchlist) {
        if (debugflag) {
            spdlog::debug("searching valid filesystems, currently {}", cfilesystem);
        }

        std::unique_ptr<Database> candidate_db;
        try {
            candidate_db = std::unique_ptr<Database>(config.openDB(cfilesystem));
        } catch (DatabaseException& e) {
            spdlog::error(e.what());
            continue;
        }

        // check if entry exists
        try {
            if (user_option.length() > 0 && (getuid() == 0))
                dbid = user_option + "-" + name;
            else
                dbid = username + "-" + name;

            entrylist.push_back(std::unique_ptr<DBEntry>(candidate_db->readEntry(dbid, false)));
            db = std::move(candidate_db);
            ws_exists = true;
        } catch (DatabaseException& e) {
            // silently ignore non existing entries
            if (debugflag)
                spdlog::debug("existence check failed for {}/{}", cfilesystem, dbid);
        }
    } // searchloop

    // make sure workspace ID is unique
    if (entrylist.size() > 1) {
        spdlog::error("aborting, there is {} workspaces with that name, please use -F to specify filesystem",
                      entrylist.size());
        return false;
    } else {
        if (entrylist.size() > 0)
            dbentry = std::move(entrylist[0]);
    }

    // workspace exists, release it

    if (ws_exists) {

        // timestamp for versioning, has to be identical for DB and workspace DIR
        time_t timestamp_time = time(NULL);

        //
        // first handle DB entry
        //

        // set expiration to now so it gets deleted earlier after beeing released
        //   FIXME: this could be obsoleted later, releasad date assures this already?
        // dbentry->setExpiration(time(NULL));

        // set released flag so released workspaces can be distinguished from expired ones,
        // update DB entry and move it
        try {
            // timestamp is version information, same as for directory later, can be modified
            // for collision avoidance!
            dbentry->release(timestamp_time);
        } catch (const DatabaseException& e) {
            spdlog::error(e.what());
            return false; // on error we bail out, workspace will still exist and db most probably as well
        }
        // we exit this as DB user on success

        //
        // second handle workspace directory
        //
        // use timestamp_time which can be modified by db->release to avoid collisions
        // new name is still identical to DB, but does not collide
        string timestamp = fmt::format("{}", timestamp_time);

        auto wsconfig = dbentry->getConfig()->getFsConfig(dbentry->getFilesystem());
        cppfs::path target = cppfs::path(dbentry->getWSPath()).parent_path() / cppfs::path(wsconfig.deletedPath) /
                             cppfs::path(fmt::format("{}-{}", dbentry->getId(), timestamp));

        caps.raise_cap({CAP_DAC_OVERRIDE}, utils::SrcPos(__FILE__, __LINE__, __func__));

        try {
            if (debugflag)
                spdlog::debug("rename({}, {})", dbentry->getWSPath(), target.string());
            cppfs::rename(dbentry->getWSPath(), target);
        } catch (const std::filesystem::filesystem_error& e) {
            if (e.code() == std::errc::cross_device_link) {
                spdlog::info("cross device rename, falling back to 'mv'");
                int ret = utils::mv(dbentry->getWSPath().c_str(), target.c_str());
                caps.lower_cap({CAP_DAC_OVERRIDE}, dbentry->getConfig()->dbuid(),
                            utils::SrcPos(__FILE__, __LINE__, __func__));
                if (ret != 0) {
                    spdlog::error("workspace directory could not be moved to deleted path via 'mv': {}", strerror(errno));
                    return false;
                }
            } else {
                caps.lower_cap({CAP_DAC_OVERRIDE}, dbentry->getConfig()->dbuid(),
                            utils::SrcPos(__FILE__, __LINE__, __func__));
                if (debugflag)
                    spdlog::error("{}", e.what());
                spdlog::error("workspace directory could not be moved to deleted path!");
                return false;
            }
        }

        caps.lower_cap({CAP_DAC_OVERRIDE}, dbentry->getConfig()->dbuid(), utils::SrcPos(__FILE__, __LINE__, __func__));

        syslog(LOG_INFO, "release for user <%s> from <%s> to <%s> done.", user::getUsername().c_str(),
               dbentry->getWSPath().c_str(), target.c_str());

        spdlog::info("workspace {} released.", name);
        if (!deletedata) {
            spdlog::info("workspace is still recoverable as --delete-data was not given.");
        } else {
            spdlog::info("workspace is not recoverable as --delete-data was given.");
        }

        //
        // third remove data is requested
        //

        if (deletedata) {
            spdlog::info("deleting workspace as --delete-data was given");
            spdlog::info("you have 5 seconds to interrupt with CTRL-C to prevent deletion");
            sleep(5);

            caps.raise_cap({CAP_FOWNER}, utils::SrcPos(__FILE__, __LINE__, __func__));
            if (caps.isSetuid()) {
                // get process owner to be allowed to delete files
                if (seteuid(getuid())) {
                    spdlog::error("can not setuid, bad installation?");
                }
            }

            // remove the directory
            std::error_code ec;
            if (debugflag) {
                spdlog::debug("rmtree({})", target.string());
            }
            utils::rmtree(target); // #66

            if (caps.isSetuid()) {
                // get root so we can drop again
                if (seteuid(0)) {
                    spdlog::error("can not setuid, bad installation?");
                }
            }

            caps.lower_cap({CAP_FOWNER}, dbentry->getConfig()->dbuid(), utils::SrcPos(__FILE__, __LINE__, __func__));

            // call again with different user
            utils::rmtree(target); // #66

            syslog(LOG_INFO, "delete-data for user <%s> from <%s>.", user::getUsername().c_str(), target.c_str());

            // remove DB entry
            dbentry->remove();

            spdlog::info("workspace {} and data deleted, can not be restored.", name);

        } // if delete-data

        // if ws_exist
    } else {
        // workspace does not exist and needs to be created

        spdlog::error("Non-existent workspace given.");
        return false;
    }

    return true;
}

int main(int argc, char** argv) {
    string name;
    string filesystem;
    string mailaddress("");
    string user_option;
    string configfile;
    bool deletedata;
    po::variables_map opt;
    std::string user_conf;

    // lower capabilities to user, before interpreting any data from user
    caps.drop_caps({CAP_DAC_OVERRIDE, CAP_CHOWN, CAP_FOWNER}, getuid(), utils::SrcPos(__FILE__, __LINE__, __func__));

    // locals settings to prevent strange effects
    utils::setCLocal();

    // set custom logging format
    utils::setupLogging(string(argv[0]));

    // check commandline, get flags which are used to create ws object or for workspace release
    commandline(opt, name, filesystem, user_option, deletedata, argc, argv, configfile);

    // find which config files to read
    //   user can change this if no setuid installation OR if root
    auto configfilestoread = std::vector<cppfs::path>{"/etc/ws.d", "/etc/ws.conf"};
    if (configfile != "") {
        if (user::isRoot() || caps.isUserMode()) {
            configfilestoread = {configfile};
        } else {
            spdlog::warn("ignored config file option!");
        }
    }

    // read the config
    auto config = Config(configfilestoread);
    if (!config.isValid()) {
        spdlog::error("No valid config file found!");
        exit(-2);
    }

    // check if user is in debugusers list
    if (!config.isDebugUser(user::getUsername())) {
        if (debugflag || traceflag) {
            spdlog::warn("debug mode disabled, not in debugusers list");
            debugflag = false;
            traceflag = false;
        }
    }

    openlog("ws_release", 0, LOG_USER); // SYSLOG

    // release workspace
    if (!release(config, opt, filesystem, name, user_option, deletedata)) {
        return -1;
    }
}
