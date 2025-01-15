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

#include <iostream>
#include <string>
#include <regex>
#include "fmt/base.h"
#include "fmt/ranges.h"

#include <syslog.h>
#include <unistd.h>
#include <time.h>

#include "build_info.h"
#include "config.h"
#include "user.h"
#include "utils.h"
#include "caps.h"

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

void commandline(po::variables_map &opt, string &name, string &target,
                    string &filesystem, bool &listflag, bool &terse, string &username,  int argc, char**argv) {
    // define all options

    po::options_description cmd_options( "\nOptions" );
    cmd_options.add_options()
            ("help,h", "produce help message")
            ("version,V", "show version")
            ("list,l", "list restorable workspaces")
            ("brief,b", "do not show unavailability date in list")
            ("name,n", po::value<string>(&name), "workspace name")
            ("target,t", po::value<string>(&target), "existing target workspace name")
            ("filesystem,F", po::value<string>(&filesystem), "filesystem")
            ("username,u", po::value<string>(&username), "username")
    ;

    po::options_description secret_options("Secret");
    secret_options.add_options()
        ("debug", "show debugging information")
        ;

    // define options without names
    po::positional_options_description p;
    p.add("name", 1);
    p.add("target", 2);

    po::options_description all_options;
    all_options.add(cmd_options).add(secret_options);

    // parse commandline
    try{
        po::store(po::command_line_parser(argc, argv).options(all_options).positional(p).run(), opt);
        po::notify(opt);
    } catch (...) {
        cout << "Usage:" << argv[0] << ": [options] workspace_name target_name | -l" << endl;
        cout << cmd_options << "\n";
        exit(1);
    }

    // see whats up

    if (opt.count("help")) {
        cout << "Usage:" << argv[0] << ": [options] workspace_name target_name | -l" << endl;
        cout << cmd_options << "\n";
                cout << "attention: the workspace_name argument is as printed by " << argv[0] << " -l not as printed by ws_list!" << endl;
        exit(0);
    }

    if (opt.count("version")) {
#ifdef IS_GIT_REPOSITORY
        cout << "workspace build from git commit hash " << GIT_COMMIT_HASH
             << " on top of release " << WS_VERSION << endl;
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

    if (opt.count("name"))
    {
        if (!opt.count("target")) {
            cout << "Error: no target given." << endl;
            cout << argv[0] << ": [options] workspace_name target_name | -l" << endl;
            cout << cmd_options << "\n";
            exit(1);
        }
        // validate workspace name against nasty characters    
        // static const std::regex e("^[a-zA-Z0-9][a-zA-Z0-9_.-]*$"); // #77
        static const std::regex e1("^[[:alnum:]][[:alnum:]_.-]*$");
        if (!regex_match(name.substr(0,2) , e1)) {
                        cerr << "Error: Illegal workspace name, use characters and numbers, -,. and _ only!" << endl;
                        exit(1);
        }
        static const std::regex e2("[^[:alnum:]_.-]");
        if (regex_search(name, e2)) {
                        cerr << "Error: Illegal workspace name, use characters and numbers, -,. and _ only!" << endl;
                        exit(1);
        }
    } else if (!opt.count("list")) {
        cout << "Error: neither workspace nor -l specified." << endl;
        cout << argv[0] << ": [options] workspace_name target_name | -l" << endl;
        cout << cmd_options << "\n";
        exit(1);
    }

}

int main(int argc, char **argv) {
    po::variables_map opt;
    string name, target, filesystem, username;
    string configfile;
    string user_conf;
    bool listflag, terse;

    // lower capabilities to user, before interpreting any data from user
    caps.drop_caps({CAP_DAC_OVERRIDE, CAP_CHOWN, CAP_FOWNER}, getuid(), utils::SrcPos(__FILE__, __LINE__, __func__));

    // locals settings to prevent strange effects
    utils::setCLocal();

    // check commandline, get flags which are used to create ws object or for workspace allocation
    commandline(opt, name, target, filesystem, listflag, terse, username, argc, argv);

    // find which config files to read
    //   user can change this if no setuid installation OR if root
    auto configfilestoread = std::vector<cppfs::path>{"/etc/ws.d","/etc/ws.conf"}; 
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
    string user_conf_filename = user::getUserhome()+"/.ws_user.conf";
    if (!cppfs::is_symlink(user_conf_filename)) {
        if (cppfs::is_regular_file(user_conf_filename)) {
            user_conf = utils::getFileContents(user_conf_filename.c_str());
        }
        // FIXME: could be parsed here and passed as object not string
    } else {
        fmt::print(stderr,"Error  : ~/.ws_user.conf can not be symlink!");
        exit(-1);
    }

    // root and admins can choose usernames
    string userpattern;                     // used for pattern matching in DB
    if (user::isRoot() || config.isAdmin(user::getUsername())) {
        if (username!="") {
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
        vector<string> validfs = config.validFilesystems(username,grouplist);
        if (filesystem != "") {
            if (canFind(validfs, filesystem)) {
                fslist.push_back(filesystem);
            } else {
                fmt::println(stderr, "Error  : invalid filesystem given.");
            }
        } else {
            fslist = validfs;
        }

        // FIXME: add pattern and sorting as in ws_list?

        // iterate over filesystems 
        for(auto const &fs: fslist) {
            if (debugflag) fmt::print("Debug  : loop over fslist {} in {}\n", fs, fslist);
            std::unique_ptr<Database> db(config.openDB(fs));

            for(auto const &id: db->matchPattern("*", userpattern, grouplist, true, false)) {
                fmt::println("{}", id);
                if (!terse) {
                    auto pos = id.rfind("-")+1;
                    time_t t = atol(id.substr(pos).c_str());
                    fmt::print("\tunavailable since : {}", std::ctime(&t));
                }
            }

        } // loop over fs

    } else {
        fmt::println("IMPLEMENT ME");
    }
}