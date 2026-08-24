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
#include <string>

#include <regex> // buggy in redhat 7

#include <syslog.h>

#include "UserConfig.h"
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
void commandline(po::variables_map& opt, string& name, int& duration, string& filesystem, bool& extension,
                 int& reminder, string& mailaddress, string& user, string& groupreadable, string& groupwritable,
                 string& comment, int argc, char** argv, std::string& userconf, std::string& configfile) {
    // define all options

    po::options_description cmd_options("\nOptions");
    // clang-format off
    cmd_options.add_options()
        ("help,h", "produce help message")("version,V", "show version")
        ("duration,d", po::value<int>(&duration), "duration in days")
        ("name,n", po::value<string>(&name), "workspace name")
        ("filesystem,F", po::value<string>(&filesystem), "filesystem name (see ws_list -l for possible values)")
        ("reminder,r", po::value<int>(&reminder), "reminder to be sent <arg> days before expiration")
        ("mailaddress,m", po::value<string>(&mailaddress), "mailaddress to send reminder to")
        ("extension,x", "extend workspace (can change mailaddress, reminder and comment as well)")
        ("username,u", po::value<string>(&user), "username")
        ("groupreadable,g", po::value<string>(&groupreadable)->implicit_value(""), "for group <arg> readable workspace, current group if none given")
        ("groupwritable,G", po::value<string>(&groupwritable)->implicit_value(""), "for group <arg> writable workspace, current group if none given")
        ("comment,c", po::value<string>(&comment), "comment")("config", po::value<string>(&configfile), "config file");
    // clang-format on

    po::options_description secret_options("Secret");
    secret_options.add_options()("debug", "show debugging information")("trace", "show calling information");

    // define options without names
    po::positional_options_description p;
    p.add("name", 1).add("duration", 1);

    po::options_description all_options;
    all_options.add(cmd_options).add(secret_options);

    // parse commandline
    try {
        po::store(po::command_line_parser(argc, argv).options(all_options).positional(p).run(), opt);
        po::notify(opt);
    } catch (...) {
        fmt::print(stderr, "Usage: {} [options] workspace_name duration\n", argv[0]);
        fmt::println(stderr, "{}", cmd_options);
        exit(1);
    }

    // see whats up

    if (opt.count("help")) {
        fmt::print(stderr, "Usage: {} [options] workspace_name duration\n", argv[0]);
        fmt::println(stderr, "{}", cmd_options);
        exit(0);
    }

    if (opt.count("version")) {
        utils::printVersion("ws_allocate");
        utils::printBuildFlags();
        exit(0);
    }

    // this allows user to extend foreign workspaces
    if (opt.count("username") && !(opt.count("extension") || getuid() == 0)) {
        spdlog::info("Ignoring username option.");
        user = "";
    }

    if (opt.count("extension")) {
        extension = true;
    } else {
        extension = false;
    }

    if (opt.count("name")) {
        // cout << " name: " << name << "\n";
    } else {
        fmt::print(stderr, "Usage: {}: [options] workspace_name duration\n", argv[0]);
        fmt::println(stderr, "{}", cmd_options);
        exit(1);
    }

    // globalflags
    debugflag = opt.count("debug");
    traceflag = opt.count("trace");

    // parse user config
    UserConfig userconfig(userconf);

    // if no group given but needed, and there is one in userconfig, take it.
    if (opt.count("groupreadable") || opt.count("groupwritable")) {
        if (groupreadable == "" && groupwritable == "" && userconfig.getGroupname() != "") {
            spdlog::info("taking group {} from ~/.ws_user.conf", userconfig.getGroupname());
            groupreadable = groupwritable = userconfig.getGroupname();
        }
    }

    // reminder check, if we have a reminder number, we need either a mailaddress argument or config file
    // with mailaddress in user home

    if (reminder == 0) {
        auto user_conf_reminder = userconfig.getReminder();
        if (user_conf_reminder != -1) {
            reminder = user_conf_reminder;
        }
    }

    if (!opt.count("mailaddress")) {
        mailaddress = userconfig.getMailaddress();

        if (mailaddress.length() > 0) {
            spdlog::info("Took email address <{}> from users config.", mailaddress);
        } else {
            mailaddress = user::getUsername();
            spdlog::info("could not read email from users config ~/.ws_user.conf.");
            spdlog::info("reminder email will be sent to local user account");
        }
    }

    // fix duration if none given and there is one in user config
    if (duration == -1) {
        if (extension && (mailaddress != "" || comment != "" || reminder != 0)) {
            spdlog::info("setting duration to 0");
            duration = 0;
        } else {
            auto userduration = userconfig.getDuration();
            if (userduration != -1) {
                spdlog::info("read duration from userconfig");
                duration = userduration;
            }
        }
    }

    // validate email
    if (mailaddress != "" && !utils::isValidEmail(mailaddress)) {
        spdlog::error("Invalid email address, ignoring and using local user account");
        mailaddress = user::getUsername();
    }

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
bool validateFsAndGroup(const Config& config, const po::variables_map& opt, const std::string username) {
    if (traceflag)
        spdlog::trace("validateFsAndGroup(username={})", username);

    // auto groupnames=getgroupnames(username); // FIXME:  use getGrouplist ?
    auto groupnames = user::getGrouplist();

    // if a group was given, check if a valid group was given
    if (opt.count("groupreadable") && opt["groupreadable"].as<string>() != "") {
        if (find(groupnames.begin(), groupnames.end(), opt["groupreadable"].as<string>()) == groupnames.end()) {
            spdlog::error("invalid group specified!");
            return false;
        }
    }
    if (opt.count("groupwritable") && opt["groupwritable"].as<string>() != "") {
        if (find(groupnames.begin(), groupnames.end(), opt["groupwritable"].as<string>()) == groupnames.end()) {
            spdlog::error("invalid group specified!");
            return false;
        }
    }

    // if the user specifies a filesystem, he must be allowed to use it
    if (opt.count("filesystem")) {
        auto validfs = config.validFilesystems(username, groupnames, ws::CREATE);
        if (!canFind(validfs, opt["filesystem"].as<string>()) && getuid() != 0) {
            spdlog::error("You are not allowed to use the specified filesystem!");
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
bool validateDuration(const Config& config, const std::string filesystem, int& duration) {
    if (traceflag)
        spdlog::trace("validateDurationAndExtensions(filesystem={},duration={})", filesystem, duration);

    // change duration if limits exceeded and warn
    // FIXME:  TODO: old code checks for user exceptions, not yet implemented
    {
        int configduration;
        if (config.getFsConfig(filesystem).maxduration > 0) {
            configduration = config.getFsConfig(filesystem).maxduration;
        } else {
            configduration = config.durationdefault();
        }
        if (getuid() != 0 && ((duration > configduration) || (duration < 0))) {
            duration = configduration;
            spdlog::error("Duration longer than allowed for this workspace");
            spdlog::error("  setting to allowed maximum of {}", duration);
        }
    }

    return true;
}

// count workspaces of user, in all valid filesystems
int countWorkspaces(const Config& config, const std::string username, const std::vector<std::string>& grouplist) {
    int counter = 0;

    // where to list from?
    vector<string> fslist;
    vector<string> validfs = config.validFilesystems(username, grouplist, ws::LIST);
    fslist = validfs;

    vector<std::unique_ptr<DBEntry>> entrylist;
    vector<std::unique_ptr<Database>> dblist;
    std::unique_ptr<Database> db;

    // iterate over filesystems
    for (auto const& fs : fslist) {
        if (debugflag)
            spdlog::debug("loop over fslist {} in {}", fs, fslist);

        try {
            db = std::unique_ptr<Database>(config.openDB(fs));
        } catch (DatabaseException& e) {
            spdlog::error(e.what());
            continue;
        }

        auto matchlist = db->matchPattern("*", "*", grouplist, false, false);
        counter += matchlist.size();
    }

    spdlog::debug("countWorkspaces: counter={}", counter);

    return counter;
}

/*
 *  allocate or extend or modify the workspace
 *  file accesses and config access are hidden in DB and config handling
 *  SPEC:CHANGE no LUA callout
 *  FIXME: make it -> int and return errors for tesing
 *  FIXME: unit test this? make smaller functions to be able to test?
 */
bool allocate(const Config& config, const po::variables_map& opt, int duration, string filesystem, const string name,
              const bool extensionflag, const int reminder, const string mailaddress, string user_option,
              const string groupreadable, const string groupwritable, const string comment) {
    if (traceflag)
        spdlog::trace("allocate({}, {}, {}, {}, {}, {}, {}, {},{}, {})", duration, filesystem, name, extensionflag,
                      reminder, mailaddress, user_option, groupreadable, groupwritable, comment);

    long exp;

    std::string username = user::getUsername(); // current user

    // get valid filesystems to bail out if there is none
    auto valid_filesystems = config.validFilesystems(user::getUsername(), user::getGrouplist(), ws::CREATE);

    if (valid_filesystems.size() == 0) {
        spdlog::error("no valid filesystems in configuration, can not allocate");
        exit(-1); // FIXME: bad for testing
    }

    // validate filesystem and group given on command line
    if (!validateFsAndGroup(config, opt, user_option)) {
        // fmt::print(stderr, "Error  : aborting!\n");
        exit(-2);
    }

    // if no filesystem provided, get valid filesystems from config, ordered: userdefault, groupdefault, globaldefault,
    // others
    vector<string> searchlist;
    if (opt.count("filesystem")) {
        searchlist.push_back(opt["filesystem"].as<string>());
    } else {
        if (extensionflag)
            searchlist =
                config.validFilesystems(user::getUsername(), user::getGrouplist(),
                                        ws::EXTEND); // FIXME: getUsername or user_option? getGrouplist uses current uid
        else
            searchlist =
                config.validFilesystems(user::getUsername(), user::getGrouplist(),
                                        ws::CREATE); // FIXME: getUsername or user_option? getGrouplist uses current uid
    }

    //
    // check if there is already too many workspaces
    //
    if (config.maxuserworkspaces() > 0) {
        auto grouplist = user::getGrouplist();
        if (countWorkspaces(config, username, grouplist) > config.maxuserworkspaces()) {
            spdlog::error("too many workspaces, exceeding global user limit, failing.");
            syslog(LOG_ERR, "user <%s> exceeding workspace number limit", username.c_str());
            return false;
        }
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
    std::vector<std::unique_ptr<DBEntry>> entrylist;

    for (std::string cfilesystem : searchlist) {
        if (debugflag) {
            spdlog::debug("searching valid filesystems, currently {}", cfilesystem);
        }

        // auto db = config.openDB(cfilesystem);
        std::unique_ptr<Database> candidate_db;
        try {
            candidate_db = std::unique_ptr<Database>(config.openDB(cfilesystem));
        } catch (DatabaseException& e) {
            spdlog::error(e.what());
            continue;
        }

        // handle extension request with username first, as it has different error handling
        if (extensionflag && user_option.length() > 0) {
            dbid = user_option + "-" + name;
            try {
                entrylist.push_back(std::unique_ptr<DBEntry>(candidate_db->readEntry(dbid, false)));
                db = std::move(candidate_db);
                foundfs = cfilesystem;
                ws_exists = true;
            } catch (DatabaseException& e) {
                // silently ignore non existiong entries
                if (debugflag)
                    spdlog::debug("existence check failed for {}/{}", cfilesystem, dbid);
            }
        } else {
            // check if entry exists
            try {
                if (user_option.length() > 0 && (getuid() == 0))
                    dbid = user_option + "-" + name;
                else
                    dbid = username + "-" + name;

                entrylist.push_back(std::unique_ptr<DBEntry>(candidate_db->readEntry(dbid, false)));
                db = std::move(candidate_db);
                foundfs = cfilesystem;
                ws_exists = true;
            } catch (DatabaseException& e) {
                // silently ignore non existiong entries
                if (debugflag)
                    spdlog::debug("existence check failed for {}/{}", cfilesystem, dbid);
            }
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

    // workspace exists, change mailaddress, reminder settings or comment, and extend if requested
    // consume an extension

    if (ws_exists) {
        auto wsdir = dbentry->getWSPath();
        int extension = 0;
        long expiration = 0;

        if (extensionflag) {
            // extension blocked by admin?
            if (!config.getFsConfig(foundfs).extendable) {
                spdlog::error("workspace can not be extended in this filesystem.");
                exit(-1); // FIXME: bad for testing
            }

            // we allow a user to specify -u -x together, and to extend a workspace if he has rights on the workspace
            if (user_option.length() > 0 && (user_option != username) && (getuid() != 0)) {
                spdlog::info("you are not owner of the workspace.");
                if (access(wsdir.c_str(), R_OK | W_OK | X_OK) != 0) {
                    spdlog::info("         and you have no permissions to access the workspace, workspace will "
                                 "not be extended.");
                    exit(-1); // FIXME: bad for testing
                }
            }
            spdlog::info("extending workspace");
            syslog(LOG_INFO, "extending <%s/%s>", foundfs.c_str(), username.c_str());

            // mail address change
            auto oldmail = dbentry->getMailaddress();
            string newmail;
            if (mailaddress != "") {
                newmail = mailaddress;
                spdlog::info("changed mail address to {}", newmail);
            } else {
                if (oldmail != "") {
                    newmail = oldmail;
                    spdlog::info("reused mail address {}", newmail);
                }
            }

            if (reminder != 0) {
                spdlog::info("changed reminder setting.");
            }
            if (comment != "") {
                spdlog::info("changed comment.");
            }

            if (duration != 0) {
                int configduration;

                // FIXME: implement userexceptions
                // if(userconfig["workspaces"][cfilesystem]["userexceptions"][username]["duration"]) {
                //    configduration =
                //    userconfig["workspaces"][cfilesystem]["userexceptions"][username]["duration"].as<int>();
                //} else {
                if ((configduration = config.getFsConfig(foundfs).maxduration) == 0) {
                    configduration = config.durationdefault();
                }
                //}
                if (getuid() != 0 && ((duration > configduration) || (duration < 0))) {
                    duration = configduration;
                    spdlog::error("Duration longer than allowed for this workspace.");
                    spdlog::error("setting to allowed maximum of {}", duration);
                }
                exp = time(NULL) + duration * 24 * 3600;
            } else {
                exp = -1;
            }

            try {
                dbentry->useExtension(exp, newmail, reminder, comment);
            } catch (const DatabaseException& e) {
                spdlog::error("{}", e.what());
                exit(-2);
            }

        } else {
            spdlog::info("reusing workspace");
            syslog(LOG_INFO, "reusing <%s/%s>.", foundfs.c_str(), dbid.c_str());
        }

        extension = dbentry->getExtension();
        expiration = dbentry->getExpiration();

        // print status, end of this invocation
        fmt::print("{}\n", wsdir);
        fmt::print(stderr, "remaining extensions  : {}\n", extension);
        fmt::print(stderr, "remaining time in days: {}\n", (expiration - time(NULL)) / (24 * 3600));

        // done

        // if ws_exist
    } else {
        // workspace does not exist and needs to be created

        if (extensionflag) {
            spdlog::error("workspace does not exist, can not be extended!");
            exit(-1);
        }

        // workspace does not exist and a new one has to be created

        std::string newfilesystem; // where to create a new workspace

        if (opt.count("filesystem")) {
            newfilesystem = opt["filesystem"].as<string>(); // commandline or
        } else {
            newfilesystem = searchlist[0]; // highest prio
        }

        // is that filesystem open for allocations?
        if (!config.getFsConfig(newfilesystem).allocatable) {
            spdlog::error("this workspace can not be used for allocation.");
            exit(-2);
        }

        // if it does not exist, create it
        spdlog::info("creating workspace.");

        // read the possible spaces for the filesystem
        vector<string> spaces = config.getFsConfig(newfilesystem).spaces;

        // SPEC:CHANGE: no LUA callouts

        // open DB where workspace will be created
        std::unique_ptr<Database> creationDB;
        try {
            creationDB = std::unique_ptr<Database>(config.openDB(newfilesystem));
        } catch (DatabaseException& e) {
            spdlog::error(e.what());
            spdlog::error("aborting");
            return false;
        }

        // check which group to use, from commandline or use current group
        string primarygroup = "";
        if (groupreadable != "" || groupwritable != "") {
            primarygroup = (groupreadable != "") ? groupreadable : groupwritable;
        } else {
            primarygroup = user::getGroupname();
        }

        // check if any group option is set
        bool groupflag = opt.count("groupreadable") || opt.count("groupwritable");

        // create workspace
        auto wsdir =
            creationDB->createWorkspace(name, user_option, groupflag, opt.count("groupwritable"), primarygroup);

        // now create DB entry

        validateDuration(config, newfilesystem, duration);

        auto extensions = config.getFsConfig(newfilesystem).maxextensions;
        auto expiration = time(NULL) + duration * 24 * 3600;

        auto id = fmt::format("{}-{}", username, name);
        creationDB->createEntry(id, wsdir, time(NULL), expiration, reminder, extensions, groupflag, primarygroup,
                                mailaddress, comment);

        fmt::print("{}\n", wsdir);
        fmt::print(stderr, "remaining extensions  : {}\n", extensions);
        fmt::print(stderr, "remaining time in days: {}\n", (expiration - time(NULL)) / (24 * 3600));

        syslog(LOG_INFO, "created for user <%s> DB <%s> with space <%s>.", username.c_str(), id.c_str(), wsdir.c_str());
    }
    return true;
}

int main(int argc, char** argv) {
    int duration = -1;
    bool extensionflag;
    string name;
    string filesystem;
    string mailaddress("");
    string user_option, groupreadable, groupwritable;
    string comment;
    string configfile;
    int reminder = 0;
    po::variables_map opt;
    std::string user_conf;

    // lower capabilities to user, before interpreting any data from user
    caps.drop_caps({CAP_DAC_OVERRIDE, CAP_CHOWN, CAP_FOWNER}, getuid(), utils::SrcPos(__FILE__, __LINE__, __func__));

    // locals settings to prevent strange effects
    utils::setCLocal();

    // set custom logging format
    utils::setupLogging(string(argv[0]));

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

    // check commandline, get flags which are used to create ws object or for workspace allocation
    commandline(opt, name, duration, filesystem, extensionflag, reminder, mailaddress, user_option, groupreadable,
                groupwritable, comment, argc, argv, user_conf, configfile);

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

    // now we have config, fix values
    if (duration == -1) {
        duration = min(config.durationdefault(), config.maxduration());
    }

    if (reminder >= duration) {
        spdlog::warn("reminder is only sent after workspace expiry!");
    }

    // check if user is in debugusers list
    if (!config.isDebugUser(user::getUsername())) {
        if (debugflag || traceflag) {
            spdlog::warn("debug mode disabled, not in debugusers list");
            debugflag = false;
            traceflag = false;
        }
    }

    openlog("ws_allocate", 0, LOG_USER); // SYSLOG

    // spdlog::info("maxuserworkspaces = {}", config.maxuserworkspaces());

    // allocate workspace
    if (!allocate(config, opt, duration, filesystem, name, extensionflag, reminder, mailaddress, user_option,
                  groupreadable, groupwritable, comment)) {
        return -1;
    }
}
