/*
 *  hpc-workspace-v2
 *
 *  ws_restore
 *
 *  - tool to list and restore expired or released workspaces
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
#include <cerrno>
#include <regex>
#include <string>

#include <syslog.h>
#include <time.h>
#include <unistd.h>

#include "build_info.h"
#include "caps.h"
#include "config.h"
#include "user.h"
#include "utils.h"
#include "ws.h"

#include <fcntl.h>

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

void commandline(po::variables_map& opt, string& name, string& target, string& filesystem, bool& listflag, bool& terse,
                 string& username, int argc, char** argv, std::string& configfile) {
    // define all options

    po::options_description cmd_options("\nOptions");
    // clang-format off
    cmd_options.add_options()
        ("help,h", "produce help message")
        ("version,V", "show version")
        ("list,l", "list restorable workspaces")
        ("brief,b", "do not show unavailability date in list")
        ("name,n", po::value<string>(&name), "workspace name")
        ("target,t", po::value<string>(&target), "existing target workspace name")
        ("filesystem,F", po::value<string>(&filesystem), "filesystem")
        ("config", po::value<string>(&configfile), "config file")
        ("username,u", po::value<string>(&username), "username")
        ("delete-data", "delete all data, workspace can NOT BE RECOVERED!");
    // clang-format on

    po::options_description secret_options("Secret");
    secret_options.add_options()("debug", "show debugging information")("trace", "show calling information");

    // define options without names
    po::positional_options_description p;
    p.add("name", 1);
    p.add("target", 2);

    po::options_description all_options;
    all_options.add(cmd_options).add(secret_options);

    // parse commandline
    try {
        po::store(po::command_line_parser(argc, argv).options(all_options).positional(p).run(), opt);
        po::notify(opt);
    } catch (...) {
        fmt::println(stderr,
                     "Usage: {} [options] workspace_name target_name | -l [pattern] | --delete-data workspace-name",
                     argv[0]);
        fmt::println(stderr, "{}", cmd_options);
        exit(1);
    }

    // see whats up

    if (opt.count("help")) {
        fmt::println(stderr,
                     "Usage: {} [options] workspace_name target_name | -l [pattern] | --delete-data workspace-name",
                     argv[0]);
        fmt::println(stderr, "{}", cmd_options);
        fmt::println(stderr,
                     "attention: the workspace_name argument is as printed by {}"
                     " -l not as printed by ws_list!",
                     argv[0]);
        exit(0);
    }

    if (opt.count("version")) {
        utils::printVersion("ws_restore");
        utils::printBuildFlags();
        exit(0);
    }

    if (opt.count("list")) {
        listflag = true;
    } else {
        listflag = false;
    }

    if (opt.count("brief")) {
        terse = true;
    } else {
        terse = false;
    }

    // globalflags
    debugflag = opt.count("debug");
    traceflag = opt.count("trace");

    if (opt.count("name")) {
        if (!opt.count("target") && !opt.count("delete-data") && !listflag) {
            spdlog::error("no target given.");
            fmt::println(stderr,
                         "Usage: {} [options] workspace_name target_name | -l [pattern] | --delete-data workspace-name",
                         argv[0]);
            fmt::println("{}", cmd_options);
            exit(1);
        }

        if (!listflag) {
            // validate workspace name against nasty characters
            // static const std::regex e("^[a-zA-Z0-9][a-zA-Z0-9_.-]*$"); // #77
            static const std::regex e1(ws::workspace_name_regex);
            if (!regex_match(name.substr(0, 2), e1)) {
                spdlog::error("Illegal workspace name, use characters and numbers, -,. and _ only!");
                exit(1);
            }
            static const std::regex e2(R"([^[:alnum:]\._-])");
            if (regex_search(name, e2)) {
                spdlog::error("Illegal workspace name, use characters and numbers, -,. and _ only!");
                exit(1);
            }
        }
    } else if (!opt.count("list")) {
        spdlog::error("neither workspace nor -l specified.");
        fmt::println(stderr,
                     "Usage: {} [options] workspace_name target_name | -l [pattern] | --delete-data workspace-name",
                     argv[0]);
        fmt::println(stderr, "{}", cmd_options);
        exit(1);
    }
}

/*
 * check that either username matches the name of the workspace, or we are root
 */
bool check_name(const string name, const string real_username) {
    // as username and id can contain -, splitting is not good here
    //  name has shape:    username-id-timestamp
    //                             ^ search for this
    // as id can contain - as well, let's compare username with start of name
    if ((real_username != "root") && (name.find(real_username + "-", 0) != 0)) {
        spdlog::error("only root can do this, or invalid workspace name!");
        return false;
    } else {
        return true;
    }
}

void restore(const string name, const string target, const string username, const Config& config,
             const string filesystem, const bool deletedata) {
    // list of groups of this process
    auto grouplist = user::getGrouplist();

    // split the name in username, name and timestamp
    // remove the username prefix to get id_noowner
    string id_noowner = name;
    if (name.compare(0, username.length(), username) == 0 && name.length() > username.length() &&
        name[username.length()] == '-') {
        id_noowner = name.substr(username.length() + 1);
    }

    // where to search for?
    vector<string> fslist;
    vector<string> validfs = config.validFilesystems(username, grouplist, ws::RESTORE);
    if (filesystem != "") {
        if (canFind(validfs, filesystem)) {
            fslist.push_back(filesystem);
        } else {
            spdlog::error("invalid filesystem given.");
            return;
        }
    } else {
        fslist = validfs;
    }

    vector<pair<string, string>> hits;

    // FIXME dbuid would be sufficient
    caps.raise_cap({CAP_DAC_OVERRIDE, CAP_DAC_READ_SEARCH}, utils::SrcPos(__FILE__, __LINE__, __func__));

    // iterate over filesystems
    for (auto const& fs : fslist) {
        if (debugflag)
            spdlog::debug("loop over fslist {} in {}", fs, fslist);

        std::unique_ptr<Database> db;
        try {
            db = std::unique_ptr<Database>(config.openDB(fs));
        } catch (DatabaseException& e) {
            spdlog::error(e.what());
            continue;
        }

        for (auto const& id : db->matchPattern(id_noowner, username, grouplist, true, false)) {
            hits.push_back({fs, id});
        }
    }
    caps.lower_cap({CAP_DAC_OVERRIDE, CAP_DAC_READ_SEARCH}, config.dbuid(),
                   utils::SrcPos(__FILE__, __LINE__, __func__));

    // exit in case not unique (unlikely due to labels with seconds precision!)
    if (hits.size() > 1) {
        spdlog::error("id {} is not unique, please give filesystem with -F!", name);
        for (const auto& h : hits) {
            spdlog::error(" {} is in {}", h.second, h.first);
        }
        exit(1);
    } else if (hits.size() == 0) {
        spdlog::error("workspace to restore does not exist!");
        exit(1);
    }

    auto source_filesystem = hits[0].first;

    if (!config.getFsConfig(source_filesystem).restorable) {
        spdlog::error("it is not possible to restore workspaces in this filesystem.");
        return;
    }

    std::unique_ptr<Database> source_db;
    try {
        source_db = std::unique_ptr<Database>(config.openDB(source_filesystem));
    } catch (DatabaseException& e) {
        spdlog::error(e.what());
        spdlog::error("aborting");
        return;
    }

    // get source entry
    std::unique_ptr<DBEntry> source_entry;
    try {
        source_entry = source_db->readEntry(name, true);
    } catch (DatabaseException& e) {
        spdlog::error("workspace does not exist!");
        return;
    }

    // SPEC:CHANGE new in V2, wipe data on demand
    if (deletedata) {

        // this is path of original workspace, from this we derive the deleted name
        string wsdir = source_entry->getWSPath();

        // go one up, add deleted subdirectory and add workspace name
        string wssourcename = cppfs::path(wsdir).parent_path().string() + "/" +
                              config.getFsConfig(source_filesystem).deletedPath + "/" + name;

        spdlog::info("deleting files as --delete-data was given");
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
        if (debugflag) {
            spdlog::debug("remove_below({})", cppfs::path(wssourcename).string());
        }
        utils::rmtree_below(wssourcename);

        if (caps.isSetuid()) {
            // get root so we can drop again
            if (seteuid(0)) {
                spdlog::error("can not setuid, bad installation?");
            }
        }
        caps.lower_cap({CAP_FOWNER}, source_entry->getConfig()->dbuid(), utils::SrcPos(__FILE__, __LINE__, __func__));

        caps.raise_cap({CAP_DAC_OVERRIDE}, utils::SrcPos(__FILE__, __LINE__, __func__));

        // FIXME: move this to deleteEntry?
        if (caps.isSetuid()) {
            // get db user to be able to unlink db entry from root_squash filesystems
            if (setegid(config.dbgid()) || seteuid(config.dbuid())) {
                spdlog::error("can not seteuid or setgid. Bad installation?");
            }
        }
        // delete toplevel, not recursively, as we are not allowed to delete/look inside
        try {
            cppfs::remove(wssourcename);
        } catch (const std::exception& e) {
            spdlog::error("remove {} -> {}", wssourcename, e.what());
        }

        if (caps.isSetuid()) {
            // get root so we can drop again
            if (seteuid(0)) {
                spdlog::error("can not setuid, bad installation?");
            }
        }
        caps.lower_cap({CAP_DAC_OVERRIDE}, source_entry->getConfig()->dbuid(),
                       utils::SrcPos(__FILE__, __LINE__, __func__));

        // remove DB entry
        try {
            source_db->deleteEntry(name, true);
            syslog(LOG_INFO, "delete for user <%s> from <%s> done, removed DB entry <%s>.", username.c_str(),
                   wssourcename.c_str(), name.c_str());
            spdlog::info("delete successful, database entry removed.");
        } catch (DatabaseException const& ex) {
            spdlog::error("error in DB entry removal, {}", ex.what());
        }
    } else { // don't delete data

        // find target workspace
        validfs = config.validFilesystems(username, grouplist, ws::RESTORE);
        std::unique_ptr<Database> db;
        std::unique_ptr<DBEntry> target_entry;
        string targetpath;
        for (auto const& fs : fslist) {
            if (debugflag)
                spdlog::debug("loop over fslist {} in {}", fs, fslist);

            std::unique_ptr<Database> candiate_db;
            try {
                candiate_db = std::unique_ptr<Database>(config.openDB(fs));
            } catch (DatabaseException& e) {
                spdlog::error(e.what());
                continue;
            }

            try {
                std::unique_ptr<DBEntry> entry(candiate_db->readEntry(fmt::format("{}-{}", username, target), false));
                target_entry = std::move(entry);
                targetpath = target_entry->getWSPath();
                db = std::move(candiate_db);
                break;
            } catch (DatabaseException& e) {
                // nothing...
            }
        }

        if (targetpath == "") {
            spdlog::error("target does not exist!");
            return;
        }

        // source and target are known now and exist both

        // this is path of original workspace, from this we derive the deleted name
        string wsdir = source_entry->getWSPath();

        // go one up, add deleted subdirectory and add workspace name
        string wssourcename = cppfs::path(wsdir).parent_path().string() + "/" +
                              config.getFsConfig(source_filesystem).deletedPath + "/" + name;

        caps.raise_cap({CAP_DAC_OVERRIDE, CAP_DAC_READ_SEARCH}, utils::SrcPos(__FILE__, __LINE__, __func__));

        if (config.restorenosub()) {
            // SPECIAL CASE
            // restore without subdirectory, e.g. for WEKA with quota
            // background: WEKA is very slow for mv from a directory without quotas to a
            // directory with quotas. But it can quickly rename a directory without quotas as a directory in a directory
            // without quotas. So when toplevel of workspace does not have quotas but workspaces have, it is better to
            // restore to the name of the new workspace instead into thet workspace. e.g.  .../ws/new will not get
            // .../ws/new/restored-XXXXX but .../ws/new
            string targetpathname = targetpath;
            // spdlog::info("restoring {} to {}", wssourcename, targetpathname);
            int ret = renameat(AT_FDCWD, wssourcename.c_str(), AT_FDCWD, targetpathname.c_str());
            if (ret == -1) {
                spdlog::error("renameat failed: {}", strerror(errno));
            }

            if (caps.isSetuid()) {
                // get db user to be able to unlink db entry from root_squash filesystems
                if (setegid(config.dbgid()) || seteuid(config.dbuid())) {
                    spdlog::error("can not seteuid or setgid. Bad installation?");
                    exit(-1);
                }
            }

            if (ret == 0) {
                // carry over group workspace flag from source entry
                string src_group = source_entry->getGroup();
                if (!src_group.empty()) {
                    target_entry->setGroup(src_group);
                    target_entry->writeEntry();
                }

                // remove DB entry
                try {
                    db->deleteEntry(name, true);
                    syslog(LOG_INFO, "restore for user <%s> from <%s> to <%s> done, removed DB entry <%s>.",
                           username.c_str(), wssourcename.c_str(), targetpathname.c_str(), name.c_str());
                    spdlog::info("restore successful, database entry removed.");
                } catch (DatabaseException const& ex) {
                    spdlog::error("error in DB entry removal, {}", ex.what());
                }
            } else {
                syslog(LOG_INFO, "restore for user <%s> from <%s> to <%s> failed, kept DB entry <%s>.",
                       username.c_str(), wssourcename.c_str(), targetpathname.c_str(), name.c_str());
                spdlog::error("moving data failed, database entry kept! {}", ret);
                ;
            }

        } else { // restore with subdirectory, the NORMAL CASE

            string targetpathname = targetpath + "/" + cppfs::path(wssourcename).filename().string();
            // do the move
            int ret = rename(wssourcename.c_str(), targetpathname.c_str());
            // some filesystems can not rename across some borders, examples are lustre DNE1 and NEC ScateFS for
            // metadata targets and WEKA for directories with quotas, for those, fall back to /bin/mv which does a copy
            // + delete under the hood
            if ((ret == -1) && (errno == EXDEV)) {
                ret = utils::mv(wssourcename.c_str(), targetpathname.c_str());
                if (ret != 0) {
                    spdlog::error("mv failed with code {}, errno: {} ({})", ret, errno, strerror(errno));
                }
            }

            // FIXME: move this to deleteEntry?
            if (caps.isSetuid()) {
                // get db user to be able to unlink db entry from root_squash filesystems
                if (setegid(config.dbgid()) || seteuid(config.dbuid())) {
                    spdlog::error("can not seteuid or setgid. Bad installation?");
                    exit(-1);
                }
            }

            if (ret == 0) {
                // remove DB entry
                try {
                    db->deleteEntry(name, true);
                    syslog(LOG_INFO, "restore for user <%s> from <%s> to <%s> done, removed DB entry <%s>.",
                           username.c_str(), wssourcename.c_str(), targetpathname.c_str(), name.c_str());
                    spdlog::info("restore successful, database entry removed.");
                } catch (DatabaseException const& ex) {
                    spdlog::error("error in DB entry removal, {}", ex.what());
                }
            } else {
                syslog(LOG_INFO, "restore for user <%s> from <%s> to <%s> failed, kept DB entry <%s>.",
                       username.c_str(), wssourcename.c_str(), targetpathname.c_str(), name.c_str());
                spdlog::error("moving data failed, database entry kept! (code: {} errno:{} {})", ret, errno, strerror(errno));
            }
        } // restorenosub

        // get user again
        if (caps.isSetuid()) {
            if (seteuid(0) || setegid(0)) {
                spdlog::error("can not seteuid or setgid. Bad installation?");
                exit(-1);
            }
        }
        caps.lower_cap({CAP_DAC_OVERRIDE, CAP_DAC_READ_SEARCH}, config.dbuid(),
                       utils::SrcPos(__FILE__, __LINE__, __func__));
    }
}

int main(int argc, char** argv) {
    po::variables_map opt;
    string name, target, filesystem, username;
    string configfile;
    string user_conf;
    string pattern;
    bool listflag, terse;

    // lower capabilities to user, before interpreting any data from user
    caps.drop_caps({CAP_DAC_OVERRIDE, CAP_DAC_READ_SEARCH}, getuid(), utils::SrcPos(__FILE__, __LINE__, __func__));

    // locals settings to prevent strange effects
    utils::setCLocal();

    // set custom logging format
    utils::setupLogging(string(argv[0]));

    // check commandline, get flags which are used to create ws object or for workspace allocation
    commandline(opt, name, target, filesystem, listflag, terse, username, argc, argv, configfile);

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

    // read user config
    string user_conf_filename = user::getUserhome() + "/.ws_user.conf";
    if (!cppfs::is_symlink(user_conf_filename)) {
        if (cppfs::is_regular_file(user_conf_filename)) {
            user_conf = utils::getFileContents(user_conf_filename.c_str());
        }
        // FIXME: could be parsed here and passed as object not string
    } else {
        spdlog::error("~/.ws_user.conf can not be symlink!");
        exit(-1);
    }

    // root and admins can choose usernames
    string userpattern; // used for pattern matching in DB
    if (user::isRoot() || config.isAdmin(user::getUsername())) {
        if (username != "") {
            userpattern = username;
        } else {
            userpattern = "*";
        }
    } else {
        userpattern = user::getUsername();
    }

    openlog("ws_restore", 0, LOG_USER); // SYSLOG

    if (listflag) {
        // list of groups of this process
        auto grouplist = user::getGrouplist();

        // where to list from?
        vector<string> fslist;
        vector<string> validfs = config.validFilesystems(username, grouplist, ws::RESTORE);
        if (filesystem != "") {
            if (canFind(validfs, filesystem)) {
                fslist.push_back(filesystem);
            } else {
                spdlog::error("invalid filesystem given.");
            }
        } else {
            fslist = validfs;
        }

        // FIXME here db user would be sufficient, raise_cap could offer a setuid interface
        // that does not require root here
        caps.raise_cap({CAP_DAC_OVERRIDE, CAP_DAC_READ_SEARCH}, utils::SrcPos(__FILE__, __LINE__, __func__));

        // if not pattern, show all entries
        if (name == "")
            pattern = "*";
        else
            pattern = name;

        // iterate over filesystems
        for (auto const& fs : fslist) {
            if (debugflag)
                spdlog::debug("loop over fslist {} in {}", fs, fslist);

            std::unique_ptr<Database> db;
            try {
                db = std::unique_ptr<Database>(config.openDB(fs));
            } catch (DatabaseException& e) {
                spdlog::error(e.what());
                continue;
            }

            try {
                for (auto const& id : db->matchPattern(pattern, userpattern, grouplist, true, false)) {
                    fmt::println("{}", id);
                    if (!terse) {
                        auto pos = id.rfind("-") + 1;
                        time_t filenametime = atol(id.substr(pos).c_str());
                        auto entry = db->readEntry(id, true);
                        time_t removetime =
                            (filenametime +
                             entry->getConfig()->getFsConfig(entry->getFilesystem()).keeptime * 24 * 3600);
                        time_t remaining = removetime - time(0L);
                        fmt::println("\tunavailable since : {}", utils::ctime(filenametime));
                        fmt::println("\trestorable until  : {} ({} days, {} hours)", utils::ctime(removetime),
                                     remaining / (24 * 3600), (remaining % (24 * 3600)) / 3600);
                        fmt::println("\tin filesystem     : {}", entry->getFilesystem());
                    }
                }
            } catch (DatabaseException& e) {
                spdlog::error("DB access error ({})", e.what());
            }
        } // loop over fs
        caps.lower_cap({CAP_DAC_OVERRIDE, CAP_DAC_READ_SEARCH}, config.dbuid(),
                       utils::SrcPos(__FILE__, __LINE__, __func__));
    } else { // no listflag

        // construct db-entry username  name
        string real_username = user::getUsername();
        if (username == "") {
            username = real_username;
        } else if (real_username != username) {
            if (real_username != "root") {
                spdlog::error("only root can do that. 2");
                username = real_username;
                exit(-1);
            }
        }
        if (check_name(name, real_username)) {
            if (cppfs::path(argv[0]).filename() == "ws_restore") {
                if (utils::new_ruh()) {
                    restore(name, target, username, config, filesystem, opt.count("delete-data"));
                } else {
                    syslog(LOG_INFO, "user <%s> failed ruh test.", username.c_str());
                }
            } else { // hack to allow test, human test can be skipped if executable has non-standard name
                restore(name, target, username, config, filesystem, opt.count("delete-data"));
            }
        }
    }
}
