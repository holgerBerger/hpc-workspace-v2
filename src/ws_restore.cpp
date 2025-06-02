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
#include "fmt/ranges.h" // IWYU pragma: keep
#include <iostream>
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

#include <boost/program_options.hpp>
namespace po = boost::program_options;
using namespace std;

#include <filesystem>
namespace cppfs = std::filesystem;

// global variables

bool debugflag = false;
bool traceflag = false;

// init caps here, when euid!=uid
Cap caps{};

void commandline(po::variables_map& opt, string& name, string& target, string& filesystem, bool& listflag, bool& terse,
                 string& username, int argc, char** argv, std::string& configfile) {
    // define all options

    po::options_description cmd_options("\nOptions");
    cmd_options.add_options()("help,h", "produce help message")("version,V",
                                                                "show version")("list,l", "list restorable workspaces")(
        "brief,b", "do not show unavailability date in list")("name,n", po::value<string>(&name), "workspace name")(
        "target,t", po::value<string>(&target),
        "existing target workspace name")("filesystem,F", po::value<string>(&filesystem),
                                          "filesystem")("config,c", po::value<string>(&configfile), "config file")(
        "username,u", po::value<string>(&username), "username")("delete-data",
                                                                "delete all data, workspace can NOT BE RECOVERED!");

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
        cerr << "Usage:" << argv[0] << ": [options] workspace_name target_name | -l" << endl;
        cerr << cmd_options << "\n";
        exit(1);
    }

    // see whats up

    if (opt.count("help")) {
        cerr << "Usage:" << argv[0] << ": [options] workspace_name target_name | -l" << endl;
        cerr << cmd_options << "\n";
        cerr << "attention: the workspace_name argument is as printed by " << argv[0]
             << " -l not as printed by ws_list!" << endl;
        exit(0);
    }

    if (opt.count("version")) {
#ifdef IS_GIT_REPOSITORY
        cout << "workspace build from git commit hash " << GIT_COMMIT_HASH << " on top of release " << WS_VERSION
             << endl;
#else
        cout << "workspace version " << WS_VERSION << endl;
#endif
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
        if (!opt.count("target") && !opt.count("delete-data")) {
            fmt::println(stderr, "Error  : no target given.");
            fmt::println(stderr, "Usage: {} [options] workspace_name target_name | -l", argv[0]);
            cerr << cmd_options << "\n";
            exit(1);
        }
        // validate workspace name against nasty characters
        // static const std::regex e("^[a-zA-Z0-9][a-zA-Z0-9_.-]*$"); // #77
        static const std::regex e1("^[[:alnum:]][[:alnum:]_.-]*$");
        if (!regex_match(name.substr(0, 2), e1)) {
            fmt::println(stderr, "Error  : Illegal workspace name, use characters and numbers, -,. and _ only!");
            exit(1);
        }
        static const std::regex e2("[^[:alnum:]_.-]");
        if (regex_search(name, e2)) {
            fmt::println(stderr, "Error  : Illegal workspace name, use characters and numbers, -,. and _ only!");
            exit(1);
        }
    } else if (!opt.count("list")) {
        cerr << "Error  : neither workspace nor -l specified." << endl;
        cerr << argv[0] << ": [options] workspace_name target_name | -l" << endl;
        cerr << cmd_options << "\n";
        exit(1);
    }
}

/*
 * check that either username matches the name of the workspace, or we are root
 */
bool check_name(const string name, const string username, const string real_username) {
    // split the name in username, name and timestamp
    auto pos = name.find("-");
    auto owner = name.substr(0, pos);

    // we checked already that only root can use another username with -u, so here
    // we know we are either root or username == real_username
    if ((username != owner) && (real_username != "root")) {
        fmt::println(stderr, "Error  : only root can do this, or invalid workspace name! username={} owner={}",
                     username, owner);
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
    auto pos = name.find("-");
    auto id_noowner = name.substr(pos + 1);

    // where to search for?
    vector<string> fslist;
    vector<string> validfs = config.validFilesystems(username, grouplist, ws::RESTORE);
    if (filesystem != "") {
        if (canFind(validfs, filesystem)) {
            fslist.push_back(filesystem);
        } else {
            fmt::println(stderr, "Error  : invalid filesystem given.");
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
            fmt::print("Debug  : loop over fslist {} in {}\n", fs, fslist);
        std::unique_ptr<Database> db(config.openDB(fs));

        for (auto const& id : db->matchPattern(id_noowner, username, grouplist, true, false)) {
            hits.push_back({fs, id});
        }
    }
    caps.lower_cap({CAP_DAC_OVERRIDE, CAP_DAC_READ_SEARCH}, config.dbuid(),
                   utils::SrcPos(__FILE__, __LINE__, __func__));

    // exit in case not unique (unlikely due to labels with second precision!)
    if (hits.size() > 1) {
        fmt::println(stderr, "Error  : id {} is not unique, please give filesystem with -F!", name);
        for (const auto& h : hits) {
            fmt::println(" {} is in {}", h.second, h.first);
        }
        return;
    } else if (hits.size() == 0) {
        fmt::println(stderr, "Error  : workspace to restore does not exist!");
        return;
    }

    auto source_filesystem = hits[0].first;

    if (!config.getFsConfig(source_filesystem).restorable) {
        fmt::println(stderr, "Error  : it is not possible to restore workspaces in this filesystem.");
        return;
    }

    std::unique_ptr<Database> source_db(config.openDB(source_filesystem));
    // get source entry
    std::unique_ptr<DBEntry> source_entry;
    try {
        source_entry = source_db->readEntry(name, true);
    } catch (DatabaseException& e) {
        fmt::println(stderr, "Error  : workspace does not exist!");
        return;
    }

    // SPEC:CHANGE new in V2, wipe data on demand
    if (deletedata) {

        // this is path of original workspace, from this we derive the deleted name
        string wsdir = source_entry->getWSPath();

        // go one up, add deleted subdirectory and add workspace name
        string wssourcename = cppfs::path(wsdir).parent_path().string() + "/" +
                              config.getFsConfig(source_filesystem).deletedPath + "/" + name;

        fmt::println(stderr, "Info   : deleting files as --delete-data was given");
        fmt::println(stderr, "Info   : you have 5 seconds to interrupt with CTRL-C to prevent deletion");
        sleep(5);

        caps.raise_cap({CAP_FOWNER}, utils::SrcPos(__FILE__, __LINE__, __func__));
        if (caps.isSetuid()) {
            // get process owner to be allowed to delete files
            if (seteuid(getuid())) {
                fmt::println(stderr, "Error  : can not setuid, bad installation?");
            }
        }

        // remove the directory
        std::error_code ec;
        if (debugflag) {
            fmt::println("Debug  : remove_all({})", cppfs::path(wssourcename).string());
        }
        cppfs::remove_all(cppfs::path(wssourcename), ec); // we ignore return wert as we expect an error return anyhow

        if (ec.value() != 0) {
            fmt::println(stderr, "Error  : unexpected error {}", ec.message());
        }

        if (caps.isSetuid()) {
            // get root so we can drop again
            if (seteuid(0)) {
                fmt::println(stderr, "Error  : can not setuid, bad installation?");
            }
        }
        caps.lower_cap({CAP_FOWNER}, source_entry->getConfig()->dbuid(), utils::SrcPos(__FILE__, __LINE__, __func__));

        // FIXME: move this to deleteEntry?
        if (caps.isSetuid()) {
            // get db user to be able to unlink db entry from root_squash filesystems
            if (setegid(config.dbgid()) || seteuid(config.dbuid())) {
                cerr << "Error: can not seteuid or setgid. Bad installation?" << endl;
                exit(-1);
            }
        }

        // remove DB entry
        try {
            source_db->deleteEntry(name, true);
            syslog(LOG_INFO, "delete for user <%s> from <%s> done, removed DB entry <%s>.", username.c_str(),
                   wssourcename.c_str(), name.c_str());
            fmt::println(stderr, "Info   : delete successful, database entry removed.");
        } catch (DatabaseException const& ex) {
            fmt::println(stderr, "Error  : error in DB entry removal, {}", ex.what());
        }
    } else { // don't delete data

        // find target workspace
        validfs = config.validFilesystems(username, grouplist, ws::RESTORE);
        std::unique_ptr<Database> db;
        string targetpath;
        for (auto const& fs : fslist) {
            if (debugflag)
                fmt::print("Debug  : loop over fslist {} in {}\n", fs, fslist);
            std::unique_ptr<Database> candiate_db(config.openDB(fs));

            try {
                std::unique_ptr<DBEntry> entry(candiate_db->readEntry(fmt::format("{}-{}", username, target), false));
                targetpath = entry->getWSPath();
                db = std::move(candiate_db);
                break;
            } catch (DatabaseException& e) {
                // nothing...
            }
        }

        if (targetpath == "") {
            fmt::println(stderr, "Error  : target does not exist!");
            return;
        }

        // source and target are known now and exist both

        // this is path of original workspace, from this we derive the deleted name
        string wsdir = source_entry->getWSPath();

        // go one up, add deleted subdirectory and add workspace name
        string wssourcename = cppfs::path(wsdir).parent_path().string() + "/" +
                              config.getFsConfig(source_filesystem).deletedPath + "/" + name;

        string targetpathname = targetpath + "/" + cppfs::path(wssourcename).filename().string();

        caps.raise_cap({CAP_DAC_OVERRIDE, CAP_DAC_READ_SEARCH}, utils::SrcPos(__FILE__, __LINE__, __func__));

        // do the move
        int ret = rename(wssourcename.c_str(), targetpathname.c_str());

        // FIXME: move this to deleteEntry?
        if (caps.isSetuid()) {
            // get db user to be able to unlink db entry from root_squash filesystems
            if (setegid(config.dbgid()) || seteuid(config.dbuid())) {
                cerr << "Error: can not seteuid or setgid. Bad installation?" << endl;
                exit(-1);
            }
        }

        if (ret == 0) {
            // remove DB entry
            try {
                db->deleteEntry(name, true);
                syslog(LOG_INFO, "restore for user <%s> from <%s> to <%s> done, removed DB entry <%s>.",
                       username.c_str(), wssourcename.c_str(), targetpathname.c_str(), name.c_str());
                fmt::println(stderr, "Info   : restore successful, database entry removed.");
            } catch (DatabaseException const& ex) {
                fmt::println(stderr, "Error  : error in DB entry removal, {}", ex.what());
            }
        } else {
            syslog(LOG_INFO, "restore for user <%s> from <%s> to <%s> failed, kept DB entry <%s>.", username.c_str(),
                   wssourcename.c_str(), targetpathname.c_str(), name.c_str());
            cerr << "Error  : moving data failed, database entry kept! " << ret << endl;
        }

        // get user again
        if (caps.isSetuid()) {
            if (seteuid(0) || setegid(0)) {
                cerr << "Error: can not seteuid or setgid. Bad installation?" << endl;
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
    bool listflag, terse;

    // lower capabilities to user, before interpreting any data from user
    caps.drop_caps({CAP_DAC_OVERRIDE, CAP_DAC_READ_SEARCH}, getuid(), utils::SrcPos(__FILE__, __LINE__, __func__));

    // locals settings to prevent strange effects
    utils::setCLocal();

    // check commandline, get flags which are used to create ws object or for workspace allocation
    commandline(opt, name, target, filesystem, listflag, terse, username, argc, argv, configfile);

    // find which config files to read
    //   user can change this if no setuid installation OR if root
    auto configfilestoread = std::vector<cppfs::path>{"/etc/ws.d", "/etc/ws.conf"};
    if (configfile != "") {
        if (user::isRoot() || caps.isUserMode()) {
            configfilestoread = {configfile};
        } else {
            fmt::print(stderr, "Warning: ignored config file option!\n");
        }
    }

    // read the config
    auto config = Config(configfilestoread);
    if (!config.isValid()) {
        fmt::println(stderr, "Error  : No valid config file found!");
        exit(-2);
    }

    // read user config
    string user_conf_filename = user::getUserhome() + "/.ws_user.conf";
    if (!cppfs::is_symlink(user_conf_filename)) {
        if (cppfs::is_regular_file(user_conf_filename)) {
            user_conf = utils::getFileContents(user_conf_filename.c_str());
        }
        // FIXME: could be parsed here and passed as object not string
    } else {
        fmt::print(stderr, "Error  : ~/.ws_user.conf can not be symlink!");
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
                fmt::println(stderr, "Error  : invalid filesystem given.");
            }
        } else {
            fslist = validfs;
        }

        // FIXME here db user would be sufficient, raise_cap could offer a setuid interface
        // that does not require root here
        caps.raise_cap({CAP_DAC_OVERRIDE, CAP_DAC_READ_SEARCH}, utils::SrcPos(__FILE__, __LINE__, __func__));

        // FIXME: add pattern and sorting as in ws_list?

        // iterate over filesystems
        for (auto const& fs : fslist) {
            if (debugflag)
                fmt::print("Debug  : loop over fslist {} in {}\n", fs, fslist);
            std::unique_ptr<Database> db(config.openDB(fs));
            try {
                for (auto const& id : db->matchPattern("*", userpattern, grouplist, true, false)) {
                    fmt::println("{}", id);
                    if (!terse) {
                        auto pos = id.rfind("-") + 1;
                        time_t t = atol(id.substr(pos).c_str());
                        fmt::print("\tunavailable since : {}", std::ctime(&t)); // contains a \n
                        fmt::println("\tin filesystem     : {}", db->readEntry(id, true)->getFilesystem());
                    }
                }
            } catch (DatabaseException& e) {
                fmt::println(stderr, "Error  : DB access error ({})", e.what());
            }
        } // loop over fs
        caps.lower_cap({CAP_DAC_OVERRIDE, CAP_DAC_READ_SEARCH}, config.dbuid(),
                       utils::SrcPos(__FILE__, __LINE__, __func__));
    } else { // listflag

        // construct db-entry username  name
        string real_username = user::getUsername();
        if (username == "") {
            username = real_username;
        } else if (real_username != username) {
            if (real_username != "root") {
                cerr << "Error: only root can do that. 2" << endl;
                username = real_username;
                exit(-1);
            }
        }
        if (check_name(name, username, real_username)) {
            if (cppfs::path(argv[0]).filename() == "ws_restore") {
                if (utils::new_ruh()) {
                    restore(name, target, username, config, filesystem, opt.count("delete-data"));
                } else {
                    syslog(LOG_INFO, "user <%s> failed ruh test.", username.c_str());
                }
            } else {
                restore(name, target, username, config, filesystem, opt.count("delete-data"));
            }
        }
    }
}
