/*
 *  hpc-workspace-v2
 *
 *  ws_find
 *
 *  - tool to find workspaces
 *    changes to workspace++:
 *      - c++ implementation (not python anymore)
 *      - search oder is better defined
 *      - fast YAML reader with rapidyaml
 *      - correct handling of group workspaces FIXME: is it broken in V1? looks like
 * 
 *  c++ version of workspace utility
 *  a workspace is a temporary directory created in behalf of a user with a limited lifetime.
 *
 *  (c) Holger Berger 2021,2023,2024
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
#include <memory>

#include <boost/program_options.hpp>
#include "config.h"

#include "build_info.h"
#include "db.h"
#include "user.h"
#include "fmt/base.h"
#include "fmt/ranges.h"
//#include "fmt/ostream.h"

#include "caps.h"

// init caps here, when euid!=uid
Cap caps{};

namespace po = boost::program_options;
using namespace std;

bool debugflag = false;
bool traceflag = false;

int main(int argc, char **argv) {

    // options and flags
    string filesystem;
    string user;
    string configfile;
    string name;
    bool listgroups=false;

    po::variables_map opts;

    // locals settings to prevent strange effects
    utils::setCLocal();

    // define options
    po::options_description cmd_options( "\nOptions" );
    cmd_options.add_options()
	("help,h", "produce help message")
	("version,V", "show version")
    ("filesystem,F", po::value<string>(&filesystem), "filesystem to search workspaces in")
	("group,g", "enable search for group workspaces")
	("user,u", po::value<string>(&user), "only show workspaces for selected user")
    ("name,n", po::value<string>(&name), "workspace name to search for")
	("config", po::value<string>(&configfile), "config file");

    po::options_description secret_options("Secret");
    secret_options.add_options()
	("debug", "show debugging information")
    ("trace", "show tracing information") ;

    // define options without names
    po::positional_options_description p;
    p.add("name", 1);

    po::options_description all_options;
    all_options.add(cmd_options).add(secret_options);

    // parse commandline
    try{
        po::store(po::command_line_parser(argc, argv).options(all_options).positional(p).run(), opts);
        po::notify(opts);
    } catch (...) {
        fmt::print("Usage: {} [options] name\n", argv[0]);
        cout << cmd_options << endl;
        exit(1);
    }

    // get flags

    listgroups = opts.count("group");

#ifndef WS_ALLOW_USER_DEBUG // FIXME: implement this in CMake
    if (user::isRoot()) {
#else
    {
#endif
        debugflag = opts.count("debug");
        traceflag = opts.count("trace");
    }

    // handle options exiting here

    if (opts.count("help")) {
        fmt::print("Usage: {} [options] name\n", argv[0]);
        cout << cmd_options << endl; 
        exit(0);
    }

    if (opts.count("version")) {
#ifdef IS_GIT_REPOSITORY
        fmt::println("workspace build from git commit hash {} on top of release {}", GIT_COMMIT_HASH,WS_VERSION);
#else
        fmt::println("workspace version {}", WS_VERSION);
#endif
        utils::printBuildFlags();
        exit(0);
    }

    if (name=="") {
        fmt::println(stderr, "Error  : no workspace name given!");
        exit(-4);
    }

    // read config 
    //   user can change this if no setuid installation OR if root
    auto configfilestoread = std::vector<cppfs::path>{"/etc/ws.d","/etc/ws.conf"}; 
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


    // root and admins can choose usernames
    string username = user::getUsername();        // used for rights checks
    string userpattern;                     // used for pattern matching in DB
    if (user::isRoot() || config.isAdmin(user::getUsername())) {
        if (user!="") {
                userpattern = user;
        } else {
                userpattern = "*";
        }
    } else {
        userpattern = username;
    }


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
            exit(-3);
        }
    } else {
        fslist = validfs;
    }

    vector<std::unique_ptr<DBEntry>> entrylist; 
    
    // iterate over filesystems and print or create list to be sorted
    for(auto const &fs: fslist) {
        if (debugflag) fmt::print("Debug  : loop over fslist {} in {}\n", fs, fslist);
        std::unique_ptr<Database> db(config.openDB(fs));
                
        // catch DB access errors, if DB directory or DB is accessible
        try {
            for(auto const &id: db->matchPattern(name, userpattern, grouplist, false, listgroups)) {
                std::unique_ptr<DBEntry> entry(db->readEntry(id, false));
                // if entry is valid
                if (entry) {
                    fmt::print("{}\n",entry->getWSPath());
                    exit(0);
                }
            }
        }
        catch (DatabaseException &e) {
            fmt::println(stderr, "{}", e.what());
            exit(-2);
        }

    } // loop fslist

    // if we get here, we did not find the workspace
    fmt::println(stderr, "Error  : workspace not found!");
    exit(-1);

}
