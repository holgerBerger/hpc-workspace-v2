/*
 *  hpc-workspace-v2
 *
 *  ws_list
 *
 *  - tool to list workspaces
 *    changes to workspace++:
 *      - c++ implementation (not python anymore)
 *      - option to run in parallel (PARALLEL CMake flag), 
 *        which helps to hide network latency of parallel filesystems
 *        needs as of 2024 tbb as dependency of std::par
 *      - fast YAML reader with rapidyaml
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

/*
    TODO:
        -l  print details about filesystems. like max extensions, max duration
        option to show deleted workspaces?

 */


#include <iostream>   // for program_options  FIXME:
#include <memory>
#include <mutex>

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
    string pattern;
    bool listgroups=false;
    bool listfilesystems=false;
    bool listfilesystemdetails=false;
    bool shortlisting=false;
    bool listexpired=false;
    bool sortbyname=false;
    bool sortbycreation=false;
    bool sortbyremaining=false;
    bool sortreverted=false;
    bool terselisting=false;
    bool verbose=false;

    po::variables_map opts;

    // locals settings to prevent strange effects
    utils::setCLocal();

    // define options
    po::options_description cmd_options( "\nOptions" );
    cmd_options.add_options()
	("help,h", "produce help message")
	("version,V", "show version")
	("filesystem,F", po::value<string>(&filesystem), "filesystem to list workspaces from")
	("group,g", "enable listing of grou workspaces")
	("listfilesystems,l", "list available filesystems")
    ("listfilesystemdetails,L", "list available filesystems with details")
	("short,s", "short listing, only workspace names")
	("user,u", po::value<string>(&user), "only show workspaces for selected user")
	("expired,e", "show expired workspaces")
	("name,N", "sort by name")
	("creation,C", "sort by creation date")
	("remaining,R", "sort by remaining time")
	("reverted,r", "revert sort")
	("terse,t", "terse listing")
	("config", po::value<string>(&configfile), "config file")
	("pattern,p", po::value<string>(&pattern), "pattern matching name (glob syntax)")
	("verbose,v", "verbose listing") ;

    po::options_description secret_options("Secret");
    secret_options.add_options()
	("debug", "show debugging information")
    ("trace", "show tracing information") ;

    // define options without names
    po::positional_options_description p;
    p.add("pattern", 1);

    po::options_description all_options;
    all_options.add(cmd_options).add(secret_options);

    // parse commandline
    try{
        po::store(po::command_line_parser(argc, argv).options(all_options).positional(p).run(), opts);
        po::notify(opts);
    } catch (...) {
        fmt::print("Usage: {} [options] [pattern]\n", argv[0]);
        cout << cmd_options << endl; // FIXME: can not be printed with fmt??
        exit(1);
    }

    // get flags

    listgroups = opts.count("group");
    listfilesystems = opts.count("listfilesystems");
    listfilesystemdetails = opts.count("listfilesystemdetails");
    shortlisting = opts.count("short");
    listexpired = opts.count("expired");
    sortbyname = opts.count("name");
    sortbycreation = opts.count("creation");
    sortbyremaining = opts.count("remaining");
    sortreverted = opts.count("reverted");
    terselisting = opts.count("terse");
    verbose = opts.count("verbose");

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
        fmt::print("Usage: {} [options] [pattern]\n", argv[0]);
        cout << cmd_options << endl; // FIXME: can not be printed with fmt??
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

    // list of fileystems or list of workspaces
    if (listfilesystems) {
        fmt::print("available filesystems (sorted according to priority):\n");
        for(auto fs: config.validFilesystems(username,grouplist)) {
            fmt::print("{}\n", fs);
        }
    } else if (listfilesystemdetails) {
        fmt::print("available filesystems (sorted according to priority):\n");
        fmt::println("{:>10}{:>10}{:>12}{:>10}{:>10}","name","duration","extensions","keeptime","comment");
        for(auto fs: config.validFilesystems(username,grouplist)) {
            auto fsc = config.getFsConfig(fs);
            fmt::print("{:>10}{:>10}{:>12}{:>10}   {}\n", fs, fsc.maxduration, fsc.maxextensions, fsc.keeptime, fsc.comment);
        }        
    } else {
        bool sort = sortbyname || sortbycreation || sortbyremaining;
        
        // if not pattern, show all entries
        if (pattern=="") pattern = "*";

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

        vector<std::unique_ptr<DBEntry>> entrylist; 
        
        // iterate over filesystems and print or create list to be sorted
        for(auto const &fs: fslist) {
            if (debugflag) fmt::print("Debug  : loop over fslist {} in {}\n", fs, fslist);
            std::unique_ptr<Database> db(config.openDB(fs));

#pragma omp parallel for schedule(dynamic)
            for(auto const &id: db->matchPattern(pattern, userpattern, grouplist, listexpired, listgroups)) {
                try {
                    std::unique_ptr<DBEntry> entry(db->readEntry(id, listexpired));
                    // if entry is valid
                    if (entry) {
#pragma omp critical
                        {
                            // if no sorting, print, otherwise append to list
                            if (!sort) {
                                if (shortlisting) {
                                    fmt::println(entry->getId());
                                } else {
                                    entry->print(verbose, terselisting);
                                }
                            } else {
                                entrylist.push_back(std::move(entry));
                            }
                        }
                    }
                } catch (DatabaseException &e) {
                    fmt::println(e.what());
                }
            }

        } // loop over fs

        // in case of sorted output, sort and print here
        if(sort) {
            if (debugflag) fmt::println(stderr, "Debug  : sorting remaining={},creation={},name={},reverse={}", sortbyremaining,sortbycreation,sortbyname,sortreverted);
            if (sortbyremaining) std::sort(entrylist.begin(), entrylist.end(), [](const auto &x, const auto &y) { return (x->getRemaining() < y->getRemaining());} ); 
            if (sortbycreation)  std::sort(entrylist.begin(), entrylist.end(), [](const auto &x, const auto &y) { return (x->getCreation() < y->getCreation());} );
            if (sortbyname)      std::sort(entrylist.begin(), entrylist.end(), [](const auto &x, const auto &y) { return (x->getId() < y->getId());} );

            if (sortreverted) {                
                std::reverse(entrylist.begin(), entrylist.end());
            }

            for(const auto &entry: entrylist) {
                if (shortlisting) {
                    fmt::println(entry->getId());
                } else {
                    entry->print(verbose, terselisting);
                }
            }
        }


    }
  
}
