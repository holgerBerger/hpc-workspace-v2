/*
 *  hpc-workspace-v2
 *
 *  ws_allocate
 *
 *  - tool to create workspaces
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
#include <string>
#include "fmt/base.h"
#include "fmt/ranges.h"

#include <regex> // buggy in redhat 7

#include <syslog.h>

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




/* 
 *  parse the commandline and see if all required arguments are passed, and check the workspace name for 
 *  bad characters
 */
void commandline(po::variables_map &opt, string &name, int &duration, string &filesystem, bool &extension, 
                    int &reminder, string &mailaddress, string &user, string &groupname, string &comment,
                    int argc, char**argv, std::string &userconf, std::string &configfile) 
{
    // define all options

    po::options_description cmd_options( "\nOptions" );
    cmd_options.add_options()
            ("help,h", "produce help message")
            ("version,V", "show version")
            ("duration,d", po::value<int>(&duration), "duration in days")
            ("name,n", po::value<string>(&name), "workspace name")
            ("filesystem,F", po::value<string>(&filesystem), "filesystem name (see ws_list -l for possible values)")
            ("reminder,r", po::value<int>(&reminder), "reminder to be sent <arg> days before expiration")
            ("mailaddress,m", po::value<string>(&mailaddress), "mailaddress to send reminder to")
            ("extension,x", "extend workspace (can change mailaddress, reminder and comment as well)")
            ("username,u", po::value<string>(&user), "username")
            ("group,g", "group readable workspace")
            ("groupname,G", po::value<string>(&groupname)->default_value(""), "for group <arg> writable workspace")
            ("comment,c", po::value<string>(&comment), "comment")
            ("config", po::value<string>(&configfile), "config file")
    ;

    po::options_description secret_options("Secret");
    secret_options.add_options()
        ("debug", "show debugging information")
        ("trace", "show calling information")
        ;

    // define options without names
    po::positional_options_description p;
    p.add("name", 1).add("duration", 1);

    po::options_description all_options;
    all_options.add(cmd_options).add(secret_options);

    // parse commandline
    try{
        po::store(po::command_line_parser(argc, argv).options(all_options).positional(p).run(), opt);
        po::notify(opt);
    } catch (...) {
        fmt::print("Usage: {} [options] workspace_name duration\n", argv[0] );
        cout << cmd_options << "\n";
        exit(1);
    }

    // see whats up

    if (opt.count("help")) {
        fmt::print("Usage: {} [options] workspace_name duration\n", argv[0]);
        cout << cmd_options << "\n";
        exit(0);
    }

    if (opt.count("version")) {
#ifdef IS_GIT_REPOSITORY
        fmt::println("workspace build from git commit hash {} on top of release {}", GIT_COMMIT_HASH, WS_VERSION);
#else
        fmt::println("workspace version {}", WS_VERSION );
#endif
        utils::printBuildFlags();
        exit(0);
    }

    // this allows user to extend foreign workspaces
    if(opt.count("username") && !( opt.count("extension") || getuid()==0 ) ) {
        fmt::print(stderr, "Info   : Ignoring username option.\n");
        user="";
    }

    if(opt.count("extension")) {
        extension = true;
    } else {
        extension = false;
    }

    if (opt.count("name"))
    {
        //cout << " name: " << name << "\n";
    } else {
        fmt::print("Usage: {}: [options] workspace_name duration\n", argv[0]);
        cout << cmd_options << "\n"; // FIXME: iostream usage
        exit(1);
    }

    // globalflags
    debugflag = opt.count("debug");
    traceflag = opt.count("trace");
    
    // parse user config
    UserConfig userconfig(userconf);

    // reminder check, if we have a reminder number, we need either a mailaddress argument or config file
    // with mailaddress in user home

    if (reminder==0) {
        reminder = userconfig.getReminder();
    }

    if(reminder>0) {
        if (!opt.count("mailaddress")) {
            mailaddress=userconfig.getMailaddress();

            if(mailaddress.length()>0) {
                fmt::println(stderr, "Info   : Took email address <{}> from users config.",  mailaddress);
            } else {
                mailaddress = user::getUsername();
                fmt::println(stderr, "Info   : could not read email from users config ~/.ws_user.conf.");
                fmt::println(stderr, "         reminder email will be sent to local user account");
            }
        }
        if (reminder>=duration) {
                fmt::println(stderr,"Warning: reminder is only sent after workspace expiry!");
        }
    } else {
        // check if mail address was set with -m but not -r
        if(opt.count("mailaddress") && !opt.count("extension")) {
            fmt::println(stderr,"Error  : You can't use the mailaddress (-m) without the reminder (-r) option.");
            exit(1);
        }
    }

    // fix duration if none given and there is one in user config
    if (duration==-1) {
        duration=userconfig.getDuration();
    }

    // validate email 
    if (mailaddress!="" && !utils::isValidEmail(mailaddress)) {
            fmt::println(stderr,"Error  : Invalid email address, ignoring and using local user account");
            mailaddress = user::getUsername();      
    }

    // validate workspace name against nasty characters    
    //  static const std::regex e("^[a-zA-Z0-9][a-zA-Z0-9_.-]*$");  // #77
    // TODO: remove regexp dependency
    static const regex e("^[[:alnum:]][[:alnum:]_.-]*$");
    if (!regex_match(name, e)) {
            fmt::println(stderr, "Error  : Illegal workspace name, use ASCII characters and numbers, '-','.' and '_' only!");
            exit(1);
    }
}


/*
 *  validate parameters vs config file
 *
 *  return true if ok and false if not
 */
bool validateFsAndGroup(const Config &config, const po::variables_map &opt, const std::string username)
{
    if (traceflag) fmt::print(stderr, "Trace  : validateFsAndGroup(username={})", username);

    //auto groupnames=getgroupnames(username); // FIXME:  use getGrouplist ?
    auto groupnames = user::getGrouplist();

    // if a group was given, check if a valid group was given
    if ( opt["groupname"].as<string>() != "" ) {
        if ( find(groupnames.begin(), groupnames.end(), opt["groupname"].as<string>()) == groupnames.end() ) {
            fmt::print(stderr, "Error  : invalid group specified!\n");
            return false;
        }
    }

    // if the user specifies a filesystem, he must be allowed to use it
    if(opt.count("filesystem")) {
        auto validfs = config.validFilesystems(username, groupnames);
        if ( !canFind(validfs, opt["filesystem"].as<string>()) && getuid()!=0 ) {
            fmt::print(stderr, "Error  : You are not allowed to use the specified filesystem!\n");
            return false;
        }
    } 

    return true;
}

/*
 * validate duration vs config
 *
 * return true if ok and false if not, changes values if out of bounds
 */
bool validateDuration(const Config &config, const std::string filesystem, int &duration) {
    if (traceflag) fmt::print(stderr, "Trace  : validateDurationAndExtensions(filesystem={},duration={})", filesystem, duration);

    // change duration if limits exceeded and warn
    // FIXME:  TODO: old code checks for user exceptions, not yet implemented
    {
        int configduration;
        if(config.getFsConfig(filesystem).maxduration>0) {
            configduration = config.getFsConfig(filesystem).maxduration;
        } else {
            configduration = config.durationdefault();
        }
        if ( getuid()!=0 && ( (duration > configduration) || (duration < 0)) ) {
            duration = configduration;
            fmt::println(stderr, "Error  : Duration longer than allowed for this workspace");
            fmt::println(stderr, "         setting to allowed maximum of {}", duration);
        }
    }

    return true;
}


/*
 *  allocate or extend or modify the workspace
 *  file accesses and config access are hidden in DB and config handling
 *  SPEC:CHANGE no LUA callout
 *  FIXME: make it -> int and return errors for tesing
 *  FIXME: unit test this? make smaller functions to be able to test?
 */
void allocate(  
            const Config &config,
            const po::variables_map &opt,  int duration, string filesystem,
            const string name, const bool extensionflag, const int reminder, 
            const string mailaddress, string user_option, const string groupname, const string comment
            ) 
{
    if (traceflag) fmt::print(stderr, "Trace  : allocate({}, {}, {}, {}, {}, {}, {}, {}, {})\n", duration, filesystem, name, extensionflag, 
                                reminder, mailaddress, user_option, groupname, comment);

    long exp;

    std::string username = user::getUsername(); // current user

    // get valid filesystems to bail out if there is none
    auto valid_filesystems = config.validFilesystems(user::getUsername(), user::getGrouplist());

    if (valid_filesystems.size()==0) {
        fmt::print(stderr, "Error: no valid filesystems in configuration, can not allocate\n");
        exit(-1); // FIXME: bad for testing
    }

    // validate filesystem and group given on command line 
    if (!validateFsAndGroup(config, opt, user_option )) {
        //fmt::print(stderr, "Error  : aborting!\n");
        exit(-2);
    }
  

    // if no filesystem provided, get valid filesystems from config, ordered: userdefault, groupdefault, globaldefault, others
    vector<string> searchlist;
    if(opt.count("filesystem")) {
        searchlist.push_back(opt["filesystem"].as<string>());
    } else {
        searchlist = config.validFilesystems(user::getUsername(), user::getGrouplist());  // FIXME: getUsername or user_option? getGrouplist uses current uid
    }

    //
    // now search for workspace in filesystem(s) and see if it exists or create it
    //

    bool ws_exists = false;
    std::string foundfs;
    
    std::unique_ptr<DBEntry> dbentry;
    std::string dbid;

    // loop over valid workspaces, and see if the workspace exists

    std::unique_ptr<Database> db;

    for (std::string cfilesystem: searchlist) {
        if (debugflag) {
            fmt::print(stderr, "Debug  : searching valid filesystems, currently {}\n", cfilesystem);
        }

        //auto db = config.openDB(cfilesystem);
        std::unique_ptr<Database> candidate_db(config.openDB(cfilesystem));

        // handle extension request with username first, as it has different error handling
        if (extensionflag && user_option.length()>0) {
            dbid = user_option+"-"+name;
            try {
                dbentry = std::unique_ptr<DBEntry>(candidate_db->readEntry(dbid, false));
                db = std::move(candidate_db);
                foundfs = cfilesystem;
                ws_exists = true;
                break;
            } catch (DatabaseException &e) {
                fmt::println(stderr, "Error  : workspace does not exist, can not be extended!");
                exit(-1); // FIXME: is exit good here?
            }
        } else {
            // check if entry exists 
            try {
                if(user_option.length()>0 && (getuid()==0)) 
                    dbid = user_option+"-"+name;
                else 
                    dbid = username+"-"+name;

                dbentry = std::unique_ptr<DBEntry>(candidate_db->readEntry(dbid, false));
                db = std::move(candidate_db);
                foundfs = cfilesystem;
                ws_exists = true;
                break;
            } catch (DatabaseException &e) {
                // silently ignore non existiong entries
                if (debugflag) fmt::print(stderr, "Debug  : existence check failed for {}/{}\n", cfilesystem, dbid);
            }   
        }
    } // searchloop

    // workspace exists, change mailaddress, reminder settings or comment, and extend if requested
    // consume an extension

    if(ws_exists) {
        auto wsdir = dbentry->getWSPath();
        int extension = 0;
        long expiration = 0;

        if(extensionflag) {
            // extension blocked by admin?
            if (!config.getFsConfig(foundfs).extendable) {
                fmt::print(stderr, "Error  : workspace can not be extended in this filesystem.\n");
                exit(-1); // FIXME: bad for testing
            }

            // we allow a user to specify -u -x together, and to extend a workspace if he has rights on the workspace
            if(user_option.length()>0 && (user_option != username) && (getuid() != 0)) {
                fmt::print(stderr, "Info   : you are not owner of the workspace.\n");
                if(access(wsdir.c_str(), R_OK|W_OK|X_OK)!=0) {
                    fmt::print(stderr, "         and you have no permissions to access the workspace, workspace will not be extended.\n");
                    exit(-1); // FIXME: bad for testing
                }
            }
            fmt::print(stderr, "Info   : extending workspace\n");
            syslog(LOG_INFO, "extending <%s/%s>", foundfs.c_str(), username.c_str());

            // mail address change
            auto oldmail = dbentry->getMailaddress();
            string newmail;
            if(mailaddress!="") {
                newmail = mailaddress;
                fmt::print(stderr, "Info   : changed mail address to {}\n", newmail);
            } else {
                if (oldmail != "") {
                    newmail = oldmail;
                    fmt::print(stderr, "Info   : reused mail address {}\n", newmail);
                }
            }

            if(reminder!=0) {
                fmt::print(stderr, "Info   : changed reminder setting.\n");
            }
            if(comment!="") {
                fmt::print(stderr, "Info   : changed comment.\n");
            }

            if (duration != 0) {
                int configduration;

                // FIXME: implement userexceptions
                //if(userconfig["workspaces"][cfilesystem]["userexceptions"][username]["duration"]) {
                //    configduration = userconfig["workspaces"][cfilesystem]["userexceptions"][username]["duration"].as<int>();
                //} else {
                if ((configduration = config.getFsConfig(foundfs).maxduration)==0) {
                    configduration = config.durationdefault();
                }
                //}
                if ( getuid()!=0 && ( (duration > configduration) || (duration < 0)) ) {
                    duration = configduration;
                    fmt::print(stderr, "Error  : Duration longer than allowed for this workspace.");
                    fmt::print(stderr, "         setting to allowed maximum of {}\n", duration);
                }
                exp = time(NULL)+duration*24*3600;
            } else {
                exp = -1;
            }

            try {
                dbentry->useExtension(exp, newmail, reminder, comment);
            } catch (const DatabaseException &e) {
                fmt::println("{}", e.what() );
                exit(-2);
            }

            //extension = dbentry->getExtension(); // for output // FIXME: see below?

        // extensionflag
        } else {   
            fmt::print(stderr, "Info   : reusing workspace\n");
            syslog(LOG_INFO, "reusing <%s/%s>.", foundfs.c_str(), dbid.c_str()); 
        }

        extension = dbentry->getExtension();
        expiration = dbentry->getExpiration();

        // print status, end of this invocation
        fmt::print("{}\n", wsdir);
        fmt::print(stderr, "remaining extensions  : {}\n", extension);
        fmt::print(stderr, "remaining time in days: {}\n", (expiration-time(NULL))/(24*3600));

        // done

    // if ws_exist
    } else {
        // workspace does not exist and needs to be created

        if (extensionflag) {
            fmt::println(stderr, "Error  : workspace does not exist, can not be extended!");
            exit(-1); 
        }

        // workspace does not exist and a new one has to be created

        std::string newfilesystem;              // where to create a new workspace

        if(opt.count("filesystem")) {
            newfilesystem = opt["filesystem"].as<string>();     // commandline or
        } else {
            newfilesystem = searchlist[0];                      // highest prio 
        }

        // is that filesystem open for allocations?
        if (!config.getFsConfig(newfilesystem).allocatable) {
            fmt::print(stderr, "Error  : this workspace can not be used for allocation.\n");
            exit(-2);            
        }

        // if it does not exist, create it
        fmt::print(stderr, "Info   : creating workspace.\n");

        // read the possible spaces for the filesystem
        vector<string> spaces = config.getFsConfig(newfilesystem).spaces;
        
        // SPEC:CHANGE: no LUA callouts

        // open DB where workspace will be created
        std::unique_ptr<Database> creationDB(config.openDB(newfilesystem));

        auto wsdir = creationDB->createWorkspace(name, user_option, opt.count("group")>0, groupname);

        // now create DB entry

        validateDuration(config, newfilesystem, duration);

        auto extensions = config.getFsConfig(newfilesystem).maxextensions;
        auto expiration = time(NULL)+duration*24*3600;
        string primarygroup = "";
        if (opt.count("group")) {
            primarygroup = user::getGroupname();
        }
		if (groupname!="") {
			primarygroup = groupname;
		}

        auto id = fmt::format("{}-{}", username, name);
        creationDB->createEntry(id, wsdir, time(NULL), expiration, reminder, extensions,
                                 primarygroup, mailaddress, comment);

        fmt::print("{}\n", wsdir);
        fmt::print(stderr, "remaining extensions  : {}\n", extensions);
        fmt::print(stderr, "remaining time in days: {}\n", (expiration-time(NULL))/(24*3600));

        syslog(LOG_INFO, "created for user <%s> DB <%s> with space <%s>.", username.c_str(), id.c_str(), wsdir.c_str());
    }

    

}



int main(int argc, char **argv) {
    int duration = -1;
    bool extensionflag;
    string name;
    string filesystem;
    string mailaddress("");
    string user_option, groupname;
    string comment;
    string configfile;
    int reminder = 0;
    po::variables_map opt;
    std::string user_conf;

    // lower capabilities to user, before interpreting any data from user
    caps.drop_caps({CAP_DAC_OVERRIDE, CAP_CHOWN, CAP_FOWNER}, getuid(), utils::SrcPos(__FILE__, __LINE__, __func__));

    // locals settings to prevent strange effects
    utils::setCLocal();

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

    // check commandline, get flags which are used to create ws object or for workspace allocation
    commandline(opt, name, duration, filesystem, extensionflag, reminder, mailaddress, 
                   user_option, groupname, comment, argc, argv, user_conf, configfile);

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

    // now we have config, fix values
    if (duration == -1) {
        duration = config.durationdefault();
    }

    openlog("ws_allocate", 0, LOG_USER); // SYSLOG

    // allocate workspace
    allocate(config, opt, duration, filesystem, name, extensionflag, reminder, mailaddress, user_option, groupname, comment);

}



