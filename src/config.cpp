/*
 *  hpc-workspace-v2
 *
 *  config.cpp
 *
 *  - deals with glocal configuration files
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

#include <algorithm>
#include <vector>

#include "fmt/base.h"
#include "fmt/ranges.h" // IWYU pragma: keep

#ifdef WS_RAPIDYAML_CONFIG
    #define RYML_USE_ASSERT 0
    #include "c4/format.hpp"
    #include "c4/std/std.hpp"
    #include "ryml.hpp"
    #include "ryml_std.hpp"
#else
    #include "yaml-cpp/yaml.h" // IWYU pragma: keep
#endif

#include "config.h"
#include "db.h"
#include "dbv1.h"
#include "utils.h"

#include <string>

#include <filesystem>
namespace cppfs = std::filesystem;

extern bool debugflag;
extern bool traceflag;

// tries to read a list of config files, in given order, can be used to check for /etc/ws.d first and /etc/ws.conf
// second stops when file can be read, but reads all files in case of directory given
//  unittest: yes
Config::Config(const std::vector<cppfs::path> configpathes) {
    // some defaults
    global.dbuid = 0;
    global.dbgid = 0;
    global.maxextensions = 10;
    global.maxduration = 100;
    global.durationdefault = 30;
    global.reminderdefault = 0;

    bool filefound = false;

    for (const auto& configpath : configpathes) {
        if (cppfs::exists(configpath)) {
            filefound = true;
            if (cppfs::is_regular_file(configpath)) {
                if (debugflag)
                    fmt::println("Debug  : Reading config file {}", configpath.string());
                string yaml = utils::getFileContents(configpath);
                readYAML(yaml);
            } else if (cppfs::is_directory(configpath)) {
                if (debugflag)
                    fmt::println("Debug  : Reading config directory {}", configpath.string());

                // sort pathes
                std::vector<std::string> pathesToSort;
                for (const auto& entry : cppfs::directory_iterator(configpath)) {
                    pathesToSort.push_back(entry.path().string());
                }
                std::sort(pathesToSort.begin(), pathesToSort.end());

                for (const auto& cfile : pathesToSort) {
                    if (cppfs::is_regular_file(cfile)) {
                        if (debugflag)
                            fmt::println("Debug  : Reading config file {}", cfile);
                        string yaml = utils::getFileContents(cfile);
                        readYAML(yaml);
                    }
                }
            } else {
                fmt::println(stderr, "Info   : Unexpected filetype of {}", configpath.string());
                exit(-1); // bail out, someone is messing around
            }
            break; // stop after first file
        }
    }

    if (!filefound) {
        isvalid = false;
        fmt::println(stderr, "Error  : None of the config file exists!");
    } else {
        validate();
    }
}

// read config from YAML node (no validation)
Config::Config(const std::string configstring) { readYAML(configstring); }

// validate config, return false if invalid
//  unitest: indirect
bool Config::validate() {
    bool valid = true;
    if (global.dbuid == 0) {
        valid = false;
        fmt::println(stderr, "Error  : No dbuid in config!");
    }
    if (global.dbgid == 0) {
        valid = false;
        fmt::println(stderr, "Error  : No dbgid in config!");
    }
    if (global.clustername.empty()) {
        valid = false;
        fmt::println(stderr, "Error  : No clustername in config!");
    }
    if (global.adminmail.empty()) {
        valid = false;
        fmt::println(stderr, "Error  : No adminmail in config!");
    }
    // SPEC:CHANGE: require default workspace
    if (global.defaultWorkspace.empty()) {
        valid = false;
        fmt::println(stderr, "Error  : No default filesystem in config!");
    }
    if (filesystems.empty()) {
        valid = false;
        fmt::println(stderr, "Error  : No filesystems in config!");
    }

    for (auto const& [fsname, fsdata] : filesystems) {
        if (fsdata.spaces.empty()) {
            valid = false;
            fmt::println(stderr, "Error  : No spaces in filesystem <> in config!", fsname);
        }
        if (fsdata.database.empty()) {
            valid = false;
            fmt::println(stderr, "Error  : No database path in filesystem <> in config!", fsname);
        }
        if (fsdata.deletedPath.empty()) {
            valid = false;
            fmt::println(stderr, "Error  : No deleted name in filesystem <> in config!", fsname);
        }
    }
    isvalid = valid;
    return valid;
}

#ifdef RYAML
// helper to read a sequence of strings from a yaml node
static void readRyamlSequence(const ryml::NodeRef parent, const std::string key, std::vector<std::string>& target) {
    if (parent.has_child(key.c_str())) {
        auto node = parent[key.c_str()];
        for (auto n : node.children()) {
            target.push_back({n.val().str, n.val().len}); // c4::cstr -> std::string
        }
    }
}

template <typename T> static void readRyamlScalar(ryml::Tree config, const char* key, T& target) {
    ryml::NodeRef root = config.rootref();
    if (root.has_child(key)) {
        auto node = config[key];
        node >> target;
    }
}

// parse YAML from a string (using yaml-cpp)
//  unittest: indirect
void Config::readYAML(string yamlstr) {

    ryml::Tree config = ryml::parse_in_place(ryml::to_substr(yamlstr)); // FIXME: error check?
    ryml::NodeRef root = config.rootref();
    ryml::NodeRef node;

    // TODO: type checks if nodes are of right type?

    // global flags

    readRyamlScalar(config, "clustername", global.clustername);
    readRyamlScalar(config, "smtphost", global.smtphost);
    readRyamlScalar(config, "mail_from", global.mail_from);
    readRyamlScalar(config, "default_workspace", global.defaultWorkspace); // SPEC:CHANGE: accept alias
    readRyamlScalar(config, "default", global.defaultWorkspace);
    readRyamlScalar(config, "duration", global.maxduration);
    readRyamlScalar(config, "durationdefault", global.durationdefault);
    readRyamlScalar(config, "reminderdefault", global.reminderdefault);
    readRyamlScalar(config, "maxextensions", global.maxextensions);
    readRyamlScalar(config, "dbuid", global.dbuid);
    readRyamlScalar(config, "dbgid", global.dbgid);

    readRyamlSequence(config, "admins", global.admins);
    readRyamlSequence(config, "adminmail", global.adminmail);

    // SPEC:CHANGE accept filesystem as alias for workspaces to better match the -F option of the tools

    if (root.has_child("workspaces") || root.has_child("filesystems")) {
        for (auto key : vector<const char*>{"workspaces", "filesystems"}) {
            if (root.has_child(key)) {

                auto list = root[key];

                for (auto it : list.children()) {
                    Filesystem_config fs;
                    auto name = it.key();
                    fs.name = {name.str, name.len}; // c4::string -> std::string

                    if (debugflag)
                        fmt::print(stderr, "Debug  : config, reading workspace {}\n", fs.name);

                    auto ws = it[fs.name.c_str()];

                    if (node = ws["deleted"]; node.has_val())
                        node >> fs.deletedPath;
                    if (node = ws["spaceselection"]; node.has_val())
                        node >> fs.spaceselection;
                    else
                        fs.spaceselection = "random";
                    if (node = ws["database"]; node.has_val())
                        node >> fs.database;
                    if (node = ws["keeptime"]; node.has_val())
                        node >> fs.keeptime;
                    if (node = ws["maxduration"]; node.has_val())
                        node >> fs.maxduration;
                    if (node = ws["maxextensions"]; node.has_val())
                        node >> fs.maxextensions;
                    else
                        fs.maxextensions = global.maxextensions;
                    if (node = ws["allocatable"]; node.has_val())
                        node >> fs.allocatable;
                    if (node = ws["extendable"]; node.has_val())
                        node >> fs.extendable;
                    if (node = ws["restorable"]; node.has_val())
                        node >> fs.restorable;
                    if (node = ws["comment"]; node.has_val())
                        node >> fs.comment;

                    readRyamlSequence(ws, "spaces", fs.spaces);
                    readRyamlSequence(ws, "groupdefault", fs.groupdefault);
                    readRyamlSequence(ws, "userdefault", fs.userdefault);
                    readRyamlSequence(ws, "user_acl", fs.user_acl);
                    readRyamlSequence(ws, "group_acl", fs.group_acl);

                    filesystems[fs.name] = fs;
                }
            }
        }
    }
}

#else

// parse YAML from a string (using yaml-cpp)
//  unittest: indirect
void Config::readYAML(const string yaml) {
    auto config = YAML::Load(yaml);
    // global flags
    if (config["clustername"])
        global.clustername = config["clustername"].as<string>();
    if (config["smtphost"])
        global.smtphost = config["smtphost"].as<string>();
    if (config["mail_from"])
        global.mail_from = config["mail_from"].as<string>();
    if (config["default_workspace"])
        global.defaultWorkspace =
            config["default_workspace"].as<string>(); // SPEC:CHANGE accept alias default_workspace
    if (config["default"])
        global.defaultWorkspace = config["default"].as<string>();
    if (config["duration"])
        global.maxduration = config["duration"].as<int>();
    if (config["durationdefault"])
        global.durationdefault = config["durationdefault"].as<int>();
    if (config["reminderdefault"])
        global.reminderdefault = config["reminderdefault"].as<int>();
    if (config["maxextensions"])
        global.maxextensions = config["maxextensions"].as<int>();
    if (config["dbuid"])
        global.dbuid = config["dbuid"].as<int>();
    if (config["dbgid"])
        global.dbgid = config["dbgid"].as<int>();
    if (config["admins"])
        global.admins = config["admins"].as<vector<string>>();
    if (config["adminmail"])
        global.adminmail = config["adminmail"].as<vector<string>>();

    // SPEC:CHANGE accept filesystem as alias for workspaces to better match the -F option of the tools
    if (config["workspaces"] || config["filesystems"]) {
        for (auto key : std::vector<string>{"workspaces", "filesystems"}) {
            if (config[key]) {

                auto list = config[key];

                for (auto it : list) {
                    Filesystem_config fs;
                    fs.name = it.first.as<string>();
                    if (debugflag)
                        fmt::print(stderr, "Debug  : config, reading workspace {}\n", fs.name);
                    auto ws = it.second;
                    if (ws["spaces"])
                        fs.spaces = ws["spaces"].as<vector<string>>();
                    if (ws["spaceselection"])
                        fs.spaceselection = ws["spaceselection"].as<string>();
                    else
                        fs.spaceselection = "random";
                    if (ws["deleted"])
                        fs.deletedPath = ws["deleted"].as<string>();
                    if (ws["database"])
                        fs.database = ws["database"].as<string>();
                    if (ws["groupdefault"])
                        fs.groupdefault = ws["groupdefault"].as<vector<string>>();
                    if (ws["userdefault"])
                        fs.userdefault = ws["userdefault"].as<vector<string>>();
                    if (ws["user_acl"])
                        fs.user_acl = ws["user_acl"].as<vector<string>>();
                    if (ws["group_acl"])
                        fs.group_acl = ws["group_acl"].as<vector<string>>();
                    if (ws["keeptime"])
                        fs.keeptime = ws["keeptime"].as<int>();
                    else
                        fs.keeptime = 10;
                    if (ws["maxduration"])
                        fs.maxduration = ws["maxduration"].as<int>();
                    else
                        fs.maxduration = 0;
                    // SPEC:CHANGE alias
                    if (ws["duration"])
                        fs.maxduration = ws["duration"].as<int>();
                    else
                        fs.maxduration = 0;
                    if (ws["maxextensions"])
                        fs.maxextensions = ws["maxextensions"].as<int>();
                    else
                        fs.maxextensions = global.maxextensions;
                    if (ws["allocatable"])
                        fs.allocatable = ws["allocatable"].as<bool>();
                    else
                        fs.allocatable = true;
                    if (ws["extendable"])
                        fs.extendable = ws["extendable"].as<bool>();
                    else
                        fs.extendable = true;
                    if (ws["restorable"])
                        fs.restorable = ws["restorable"].as<bool>();
                    else
                        fs.restorable = true;
                    if (ws["comment"])
                        fs.comment = ws["comment"].as<string>();
                    filesystems[fs.name] = fs;
                }
            }
        }
    }
}
#endif

// is user admin?
// unittest: yes
bool Config::isAdmin(const string user) const {
    return std::find(global.admins.begin(), global.admins.end(), user) != global.admins.end();
}

// check if given user can assess given filesystem with current config
//  see validFilesystems for specification of ACLs
// unittest: yes
bool Config::hasAccess(const string user, const vector<string> groups, const string filesystem,
                       const ws::intent intent) const {
    if (traceflag)
        fmt::print(stderr, "Trace  : hasAccess(user={},groups={},filesystem={})\n", user, groups, filesystem);

    bool ok = true;

    // see if FS is valid
    if (filesystems.count(filesystem) < 1) {
        fmt::print(stderr, "Error  : invalid filesystem queried for access: {}\n", filesystem);
        return false;
    }

    // check ACLs, group first, user second to allow -user to override group grant
    if (filesystems.at(filesystem).user_acl.size() > 0 || filesystems.at(filesystem).group_acl.size() > 0) {
        // as soon as any ACL is present access is denied and has to be granted
        ok = false;
        if (debugflag)
            fmt::print(stderr, "Debug  :  ACLs present\n");

        if (filesystems.at(filesystem).group_acl.size() > 0) {
            if (debugflag)
                fmt::print(stderr, "Debug  :    group ACL present,");
            auto aclmap = utils::parseACL(filesystems.at(filesystem).group_acl);
            for (const auto& group : groups) {
                if (aclmap.count(group) > 0) {
                    auto perm = aclmap[group];
                    if (perm.second.size() == 0) {
                        ok = (perm.first == "+");
                    } else {
                        if (perm.first == "+") {
                            ok = canFind(perm.second, intent);
                        } else {
                            ok = !canFind(perm.second, intent);
                        }
                    }
                    if (debugflag)
                        fmt::print(stderr, "Debug  :    access for {} {}\n", group, ok ? "granted" : "denied");
                }
            }
            /*  old cold old ACL syntax
            for(const auto & group : groups) {
                if (canFind(filesystems.at(filesystem).group_acl, group)) ok = true;
                if (canFind(filesystems.at(filesystem).group_acl, string("+")+group)) ok = true;
                if (canFind(filesystems.at(filesystem).group_acl, string("-")+group)) ok = false;
                if (debugflag) fmt::print(stderr, "Debug  :    access for {} {}\n", group, ok?"granted":"denied");
            }
            */
        }

        if (filesystems.at(filesystem).user_acl.size() > 0) {
            if (debugflag)
                fmt::print(stderr, "Debug  :    user ACL present,");
            auto aclmap = utils::parseACL(filesystems.at(filesystem).user_acl);
            if (aclmap.count(user) > 0) {
                auto perm = aclmap[user];
                if (perm.second.size() == 0) {
                    ok = (perm.first == "+");
                } else {
                    if (perm.first == "+") {
                        ok = canFind(perm.second, intent);
                    } else {
                        ok = !canFind(perm.second, intent);
                    }
                }
                if (debugflag)
                    fmt::print(stderr, "Debug  :    access for {} {}\n", user, ok ? "granted" : "denied");
            }

            /* old code for old ACL syntax
            if (canFind(filesystems.at(filesystem).user_acl, user)) ok = true;
            if (canFind(filesystems.at(filesystem).user_acl, string("+")+user)) ok = true;
            if (canFind(filesystems.at(filesystem).user_acl, string("-")+user)) ok = false;
            if (debugflag) fmt::print(stderr,"Debug  :    access for {} {}\n", user, ok?"granted":"denied");
            */
        }
    }

    // check admins list, admins can see and access all filesystems
    if (global.admins.size() > 0) {
        if (debugflag)
            fmt::print(stderr, "Debug  :    admin list present, ");
        if (canFind(global.admins, user))
            ok = true;
        if (debugflag)
            fmt::print(stderr, "Debug  :    access {}\n", ok ? "granted" : "denied");
    }

    if (debugflag)
        fmt::print(stderr, "Debug  : => access to <{}> for user <{}> {}\n", filesystem, user,
                   ok ? "granted" : "denied");

    return ok;
}

// get list of all filesystems
vector<string> Config::Filesystems() const {
    vector<string> fslist;
    for (auto const& [fs, val] : filesystems) {
        fslist.push_back(fs);
    }
    return fslist;
}

// get list of valid filesystems for given user, each filesystem is only once in the list
//  SPEC: validFilesystems(user, groups)
//  SPEC: this list is sorted: userdefault, groupdefault, global default, others
//  SPEC:CHANGE: a user has to be able to access global default filesystem, otherwise it will be not returned here
//  SPEC:CHANGE: a user or group acl can contain a username with - prefixed, to disallow access
//  SPEC:CHANGE: a user or group acl can contain a username with + prefix, to allow access, same as only listing
//  user/group SPEC: as soon as an ACL exists, access is denied to those not in ACL SPEC: user acls are checked after
//  groups for - entries, so users can be excluded after having group access SPEC:CHANGE: a user default does NOT
//  override an ACL SPEC: admins have access to all filesystems SPEC: exhanced ACL syntax:
//  [+|-]id[:[permission{,permission}]] SPEC: permission is one of  list,use,create,extend,release,restore SPEC: if no
//  permisson is given, all permission are granted or denied
// unittest: yes
vector<string> Config::validFilesystems(const string user, const vector<string> groups, const ws::intent intent) const {
    if (traceflag)
        fmt::print(stderr, "Trace  : validFilesystems(user={},groups={})\n", user, groups);
    vector<string> validfs;

    if (debugflag) {
        // avoid vector<> fmt::print for clang <=17 at least
        fmt::print(stderr, "Debug  : validFilesystems({},{}) over ", user, groups);
        for (const auto& [fs, val] : filesystems)
            fmt::print("{} ", fs);
        fmt::print("\n");
    }

    // check if group or user default, user first
    // SPEC: with users first a workspace with user default is always in front of a groupdefault
    for (auto const& [fs, val] : filesystems) {
        if (debugflag)
            fmt::print("Debug  : fs={} filesystems.at(fs).userdefault={}\n", fs, filesystems.at(fs).userdefault);
        if (canFind(filesystems.at(fs).userdefault, user)) {
            if (debugflag)
                fmt::print(stderr, "Debug  :  checking if userdefault <{}> already added\n", fs);
            if (hasAccess(user, groups, fs, intent) && !canFind(validfs, fs)) {
                if (debugflag)
                    fmt::print(stderr, "Debug  :    adding userdefault <{}>\n", fs);
                validfs.push_back(fs);
                break;
            }
        }
    }

    // now groups
    for (auto const& [fs, val] : filesystems) {
        for (string group : groups) {
            if (debugflag)
                fmt::print(stderr, "Debug  :  checking if group <{}> in groupdefault[{}]={}\n", group, fs,
                           filesystems.at(fs).groupdefault);
            if (canFind(filesystems.at(fs).groupdefault, group)) {
                if (hasAccess(user, groups, fs, intent) && !canFind(validfs, fs)) {
                    if (debugflag)
                        fmt::print(stderr, "Debug  :    adding groupdefault <{}>\n", fs);
                    validfs.push_back(fs);
                    goto groupend;
                }
            }
        }
    }
groupend:

    // global default last
    if (debugflag) {
        fmt::println(stderr, "Debug  : global.default_workspace={}", global.defaultWorkspace);
        fmt::println(stderr, "Debug  : hasAccess({}, {}, {})={}", user, groups, global.defaultWorkspace,
                     hasAccess(user, groups, global.defaultWorkspace, intent));
        fmt::println(stderr, "Debug  : canFind({}, {})={}", validfs, global.defaultWorkspace,
                     canFind(validfs, global.defaultWorkspace));
    }
    if ((global.defaultWorkspace != "") && hasAccess(user, groups, global.defaultWorkspace, intent) &&
        !canFind(validfs, global.defaultWorkspace)) {
        if (debugflag)
            fmt::print(stderr, "Debug  :  adding default_workspace <{}>\n", global.defaultWorkspace);
        validfs.push_back(global.defaultWorkspace);
    }

    // now all others with access
    for (auto const& [fs, val] : filesystems) {
        if (hasAccess(user, groups, fs, intent) && !canFind(validfs, fs)) {
            if (debugflag)
                fmt::print(stderr, "Debug  :    adding as having access <{}>\n", fs);
            validfs.push_back(fs);
        }
    }

    if (debugflag)
        fmt::print(stderr, "Debug  : => valid filesystems {}\n", validfs);

    return validfs;
}

// get DB type for the fs
Database* Config::openDB(const string fs) const {
    if (traceflag)
        fmt::print(stderr, "Trace  : opendb {}\n", fs);
    // TODO: version check here to determine which DB to open
    return new FilesystemDBV1(this, fs);
}

// return path to database for given filesystem, or empy string
string Config::database(const string filesystem) const {
    auto it = filesystems.find(filesystem);
    if (it == filesystems.end())
        return string("");
    else
        return it->second.database;
}

// return path to deletedpath for given filesystem, or empty string
string Config::deletedPath(const string filesystem) const {
    auto it = filesystems.find(filesystem);
    if (it == filesystems.end())
        return string("");
    else
        return it->second.deletedPath;
}

// return config of filesystem throw if invalid
Filesystem_config Config::getFsConfig(const std::string filesystem) const {
    if (traceflag)
        fmt::print(stderr, "Trace  : getFsConfig({})\n", filesystem);
    try {
        return filesystems.at(filesystem);
    } catch (const std::out_of_range& e) {
        fmt::print(stderr, "no valid filesystem ({}) given in getFsConfig(), should not happen\n", filesystem);
        exit(-1); // should not be reached
    }
}
