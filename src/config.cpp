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
 *  (c) Holger Berger 2021,2023,2024,2025,2026
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
#include <map>
#include <vector>

#include "fmt/base.h"
#include "fmt/ranges.h" // IWYU pragma: keep

#include "spdlog/spdlog.h"

#include <glaze/glaze.hpp>
#include <glaze/yaml.hpp>

#include "config.h"
#include "db.h"
#include "dbv1.h"
#include "utils.h"

#include <string>

#include <filesystem>
namespace cppfs = std::filesystem;

extern bool debugflag;
extern bool traceflag;
extern int debuglevel;

// ----- Glaze structs for config parsing -----

struct FsConfig_GLZ {
    std::vector<std::string> spaces;
    std::string spaceselection = "random";
    std::string deleted;
    std::string database;
    std::vector<std::string> groupdefault;
    std::vector<std::string> userdefault;
    std::vector<std::string> user_acl;
    std::vector<std::string> group_acl;
    int keeptime = 10;
    int releasekeeptime = -1; // -1 = not set, defaults to keeptime
    int maxduration = 0;
    int maxextensions = -1;   // -1 = not set, defaults to global
    bool allocatable = true;
    bool extendable = true;
    bool restorable = true;
    std::string comment;
};

struct GlobalConfig_GLZ {
    std::string clustername;
    std::string smtphost;
    std::string mail_from;
    std::string default_workspace;
    int maxduration = 0;
    int durationdefault = 30;
    int reminderdefault = 0;
    int maxextensions = 10;
    int dbuid = 0;
    int dbgid = 0;
    int deldirtimeout = 0;
    std::string expirerlogpath;
    int maxuserworkspaces = 0;
    std::vector<std::string> admins;
    std::vector<std::string> debugusers;
    std::vector<std::string> adminmail;
    std::map<std::string, FsConfig_GLZ> workspaces;
    std::map<std::string, FsConfig_GLZ> filesystems;
};

template <>
struct glz::meta<FsConfig_GLZ> {
    using T = FsConfig_GLZ;
    static constexpr auto value = glz::object(
        "spaces",         &T::spaces,
        "spaceselection", &T::spaceselection,
        "deleted",        &T::deleted,
        "database",       &T::database,
        "groupdefault",   &T::groupdefault,
        "userdefault",    &T::userdefault,
        "user_acl",       &T::user_acl,
        "group_acl",      &T::group_acl,
        "keeptime",       &T::keeptime,
        "releasekeeptime",&T::releasekeeptime,
        "maxduration",    &T::maxduration,
        "duration",       &T::maxduration,
        "maxextensions",  &T::maxextensions,
        "allocatable",    &T::allocatable,
        "extendable",     &T::extendable,
        "restorable",     &T::restorable,
        "comment",        &T::comment
    );
    static constexpr auto opts = glz::opts{.error_on_unknown_keys = false};
};

template <>
struct glz::meta<GlobalConfig_GLZ> {
    using T = GlobalConfig_GLZ;
    static constexpr auto value = glz::object(
        "clustername",       &T::clustername,
        "smtphost",          &T::smtphost,
        "mail_from",         &T::mail_from,
        "default_workspace", &T::default_workspace,
        "default",           &T::default_workspace,
        "duration",          &T::maxduration,
        "maxduration",       &T::maxduration,
        "durationdefault",   &T::durationdefault,
        "reminderdefault",   &T::reminderdefault,
        "maxextensions",     &T::maxextensions,
        "dbuid",             &T::dbuid,
        "dbgid",             &T::dbgid,
        "deldirtimeout",     &T::deldirtimeout,
        "expirerlogpath",    &T::expirerlogpath,
        "maxuserworkspaces", &T::maxuserworkspaces,
        "admins",            &T::admins,
        "debugusers",        &T::debugusers,
        "adminmail",         &T::adminmail,
        "workspaces",        &T::workspaces,
        "filesystems",       &T::filesystems
    );
    static constexpr auto opts = glz::opts{.error_on_unknown_keys = false};
};

// Pre-process the YAML string to normalize YAML 1.1 booleans (yes/no/on/off)
// to YAML 1.2 equivalents (true/false) so Glaze can parse them as bools.
static std::string normalizeYamlBooleans(const std::string& input) {
    std::string result = input;
    const char* pairs[][2] = {
        {"yes",  "true"},  {"no",   "false"},
        {"Yes",  "true"},  {"No",   "false"},
        {"YES",  "true"},  {"NO",   "false"},
        {"on",   "true"},  {"off",  "false"},
        {"On",   "true"},  {"Off",  "false"},
        {"ON",   "true"},  {"OFF",  "false"},
    };
    for (auto [oldW, newW] : pairs) {
        std::string oldStr(oldW);
        std::string newStr(newW);
        size_t searchPos = 0;
        while ((searchPos = result.find(oldStr, searchPos)) != std::string::npos) {
            // Check it's after ": "
            if (searchPos >= 2 && result[searchPos - 2] == ':' && result[searchPos - 1] == ' ') {
                size_t endPos = searchPos + oldStr.size();
                bool atEnd = endPos >= result.size();
                if (!atEnd) {
                    char after = result[endPos];
                    if (after != ' ' && after != '\t' && after != '\n' && after != '\r' && after != '#' && after != ',') {
                        searchPos += oldStr.size();
                        continue;
                    }
                }
                result.replace(searchPos, oldStr.size(), newStr);
                searchPos += newStr.size();
                continue;
            }
            searchPos += oldStr.size();
        }
    }
    return result;
}

// ----- Config constructors -----

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
    global.maxuserworkspaces = 0;

    bool filefound = false;

    for (const auto& configpath : configpathes) {
        if (cppfs::exists(configpath)) {
            filefound = true;
            if (cppfs::is_regular_file(configpath)) {
                if (debugflag)
                    spdlog::debug("Reading config file {}", configpath.string());
                string yaml = utils::getFileContents(configpath);
                readYAML(yaml);
            } else if (cppfs::is_directory(configpath)) {
                if (debugflag)
                    spdlog::debug("Reading config directory {}", configpath.string());

                // sort pathes
                std::vector<std::string> pathesToSort;
                for (const auto& entry : cppfs::directory_iterator(configpath)) {
                    pathesToSort.push_back(entry.path().string());
                }
                std::sort(pathesToSort.begin(), pathesToSort.end());

                for (const auto& cfile : pathesToSort) {
                    if (cppfs::is_regular_file(cfile)) {
                        if (debugflag)
                            spdlog::debug("Reading config file {}", cfile);
                        string yaml = utils::getFileContents(cfile);
                        readYAML(yaml);
                    }
                }
            } else {
                spdlog::info("Unexpected filetype of {}", configpath.string());
                exit(-1); // bail out, someone is messing around
            }
            break; // stop after first file
        }
    }

    if (!filefound) {
        isvalid = false;
        spdlog::error("None of the config files exists!");
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
        spdlog::error("No dbuid in config!");
    }
    if (global.dbgid == 0) {
        valid = false;
        spdlog::error("No dbgid in config!");
    }
    if (global.clustername.empty()) {
        valid = false;
        spdlog::error("No clustername in config!");
    }
    if (global.adminmail.empty()) {
        valid = true;
        spdlog::warn("No adminmail in config!");
    }
    if (global.maxuserworkspaces == 0) {
        if (debugflag)
            spdlog::debug("maxuserworkspaces not set, using 0 (no limit)");
    }
    // SPEC:CHANGE: require default workspace
    if (global.defaultWorkspace.empty()) {
        valid = false;
        spdlog::error("No default filesystem in config!");
    }
    if (filesystems.empty()) {
        valid = false;
        spdlog::error("No filesystems in config!");
    }

    for (auto const& [fsname, fsdata] : filesystems) {
        if (fsdata.spaces.empty()) {
            valid = false;
            spdlog::error("No spaces in filesystem <> in config!", fsname);
        }
        if (fsdata.database.empty()) {
            valid = false;
            spdlog::error("No database path in filesystem <> in config!", fsname);
        }
        if (fsdata.deletedPath.empty()) {
            valid = false;
            spdlog::error("No deleted name in filesystem <> in config!", fsname);
        }
    }
    isvalid = valid;
    return valid;
}

// parse YAML from a string (using Glaze)
//  unittest: indirect
void Config::readYAML(string yamlstr) {
    std::string normalized = normalizeYamlBooleans(yamlstr);

    // Strip unknown top-level keys to avoid Glaze error_on_unknown_keys
    // Known keys: clustername, smtphost, mail_from, default_workspace, default, duration, maxduration,
    //             durationdefault, reminderdefault, maxextensions, dbuid, dbgid, deldirtimeout,
    //             expirerlogpath, maxuserworkspaces, admins, debugusers, adminmail, workspaces, filesystems
    static const std::vector<std::string> known_keys = {
        "clustername", "smtphost", "mail_from", "default_workspace", "default", "duration", "maxduration",
        "durationdefault", "reminderdefault", "maxextensions", "dbuid", "dbgid", "deldirtimeout",
        "expirerlogpath", "maxuserworkspaces", "admins", "debugusers", "adminmail", "workspaces", "filesystems",
        "expirerlogpart" // legacy alias
    };

    GlobalConfig_GLZ gcfg{};
    auto ec = glz::read_yaml<glz::opts{.error_on_unknown_keys = false}>(gcfg, normalized);
    if (ec) {
        spdlog::error("Config parse error: {}", glz::format_error(ec, normalized));
        isvalid = false;
        return;
    }

    // Copy global fields (only override if non-default/non-empty)
    if (!gcfg.clustername.empty())       global.clustername = gcfg.clustername;
    if (!gcfg.smtphost.empty())          global.smtphost = gcfg.smtphost;
    if (!gcfg.mail_from.empty())         global.mail_from = gcfg.mail_from;
    if (!gcfg.default_workspace.empty()) global.defaultWorkspace = gcfg.default_workspace;
    if (!gcfg.expirerlogpath.empty())    global.expirerlogpath = gcfg.expirerlogpath;

    if (gcfg.maxduration != 0)          global.maxduration = gcfg.maxduration;
    if (gcfg.durationdefault != 30)     global.durationdefault = gcfg.durationdefault;
    if (gcfg.reminderdefault != 0)      global.reminderdefault = gcfg.reminderdefault;
    if (gcfg.maxextensions != 10)       global.maxextensions = gcfg.maxextensions;
    if (gcfg.dbuid != 0)                global.dbuid = gcfg.dbuid;
    if (gcfg.dbgid != 0)                global.dbgid = gcfg.dbgid;
    if (gcfg.deldirtimeout != 0)        global.deldirtimeout = gcfg.deldirtimeout;
    if (gcfg.maxuserworkspaces != 0)    global.maxuserworkspaces = gcfg.maxuserworkspaces;

    if (!gcfg.admins.empty())           global.admins = gcfg.admins;
    if (!gcfg.debugusers.empty())       global.debugusers = gcfg.debugusers;
    if (!gcfg.adminmail.empty())        global.adminmail = gcfg.adminmail;

    // Merge workspaces + filesystems into Config::filesystems map
    auto mergeFsMap = [&](const std::map<std::string, FsConfig_GLZ>& src) {
        for (const auto& [k, v] : src) {
            auto it = filesystems.find(k);
            Filesystem_config& fs = (it != filesystems.end()) ? it->second : (filesystems[k]);
            fs.name = k;

            if (debugflag && debuglevel > 0)
                spdlog::debug("config, reading workspace {} with glaze", k);

            if (!v.spaceselection.empty())
                fs.spaceselection = v.spaceselection;
            if (!v.spaces.empty())          fs.spaces = v.spaces;
            if (!v.deleted.empty())         fs.deletedPath = v.deleted;
            if (!v.database.empty())        fs.database = v.database;
            if (!v.groupdefault.empty())    fs.groupdefault = v.groupdefault;
            if (!v.userdefault.empty())     fs.userdefault = v.userdefault;
            if (!v.user_acl.empty())        fs.user_acl = v.user_acl;
            if (!v.group_acl.empty())       fs.group_acl = v.group_acl;

            if (v.keeptime != 10)           fs.keeptime = v.keeptime;
            if (v.releasekeeptime >= 0)     fs.releasekeeptime = v.releasekeeptime;
            else                           fs.releasekeeptime = fs.keeptime;

            if (v.maxduration != 0)         fs.maxduration = v.maxduration;

            if (v.maxextensions >= 0)       fs.maxextensions = v.maxextensions;
            else                           fs.maxextensions = global.maxextensions;

            fs.allocatable = v.allocatable;
            fs.extendable  = v.extendable;
            fs.restorable  = v.restorable;

            fs.comment = v.comment;

            if (fs.maxduration == 0 && global.maxduration != 0) {
                fs.maxduration = global.maxduration;
            }
        }
    };

    mergeFsMap(gcfg.workspaces);
    mergeFsMap(gcfg.filesystems);
}

// is user admin?
// unittest: yes
bool Config::isAdmin(const string user) const {
    return std::find(global.admins.begin(), global.admins.end(), user) != global.admins.end();
}

// is user in debugusers list?
bool Config::isDebugUser(const string user) const {
    if (user == "root")
        return true;
    return std::find(global.debugusers.begin(), global.debugusers.end(), user) != global.debugusers.end();
}

// check if given user can assess given filesystem with current config
//  see validFilesystems for specification of ACLs
// unittest: yes
bool Config::hasAccess(const string user, const vector<string> groups, const string filesystem,
                       const ws::intent intent) const {
    if (traceflag)
        spdlog::trace("hasAccess(user={},groups={},filesystem={})", user, groups, filesystem);

    bool ok = true;

    // see if FS is valid
    if (filesystems.count(filesystem) < 1) {
        spdlog::error("invalid filesystem queried for access: {}", filesystem);
        return false;
    }

    // check ACLs, group first, user second to allow -user to override group grant
    if (filesystems.at(filesystem).user_acl.size() > 0 || filesystems.at(filesystem).group_acl.size() > 0) {
        // as soon as any ACL is present access is denied and has to be granted
        ok = false;
        if (debugflag && debuglevel > 0)
            spdlog::debug("ACLs present");

        if (filesystems.at(filesystem).group_acl.size() > 0) {
            if (debugflag && debuglevel > 0)
                spdlog::debug("   group ACL present,");
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
                    if (debugflag && debuglevel > 0)
                        spdlog::debug("   access for {} {}", group, ok ? "granted" : "denied");
                }
            }
        }

        if (filesystems.at(filesystem).user_acl.size() > 0) {
            if (debugflag && debuglevel > 0)
                spdlog::debug("   user ACL present,");
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
                if (debugflag && debuglevel > 0)
                    spdlog::debug("   access for {} {}", user, ok ? "granted" : "denied");
            }
        }
    }

    // check admins list, admins can see and access all filesystems
    if (global.admins.size() > 0) {
        if (debugflag && debuglevel > 0)
            spdlog::debug("   admin list present, ");
        if (canFind(global.admins, user))
            ok = true;
        if (debugflag && debuglevel > 0)
            spdlog::debug("   access {}", ok ? "granted" : "denied");
    }

    if (debugflag)
        spdlog::debug("=> access to <{}> for user <{}> {}", filesystem, user, ok ? "granted" : "denied");

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
        spdlog::trace("validFilesystems(user={},groups={})", user, groups);
    vector<string> validfs;

    if (debugflag && debuglevel > 0) {
        // avoid vector<> fmt::print for clang <=17 at least
        spdlog::debug("validFilesystems({},{}) over ", user, groups);
        for (const auto& [fs, val] : filesystems)
            spdlog::debug("{} ", fs);
    }

    // check if group or user default, user first
    // SPEC: with users first a workspace with user default is always in front of a groupdefault
    for (auto const& [fs, val] : filesystems) {
        if (debugflag && debuglevel > 0)
            spdlog::debug("fs={} filesystems.at(fs).userdefault={}", fs, filesystems.at(fs).userdefault);
        if (canFind(filesystems.at(fs).userdefault, user)) {
            if (debugflag && debuglevel > 0)
                spdlog::debug(" checking if userdefault <{}> already added", fs);
            if (hasAccess(user, groups, fs, intent) && !canFind(validfs, fs)) {
                if (debugflag && debuglevel > 0)
                    spdlog::debug("   adding userdefault <{}>", fs);
                validfs.push_back(fs);
                break;
            }
        }
    }

    // now groups
    for (auto const& [fs, val] : filesystems) {
        for (string group : groups) {
            if (debugflag && debuglevel > 0)
                spdlog::debug("checking if group <{}> in groupdefault[{}]={}", group, fs,
                              filesystems.at(fs).groupdefault);
            if (canFind(filesystems.at(fs).groupdefault, group)) {
                if (hasAccess(user, groups, fs, intent) && !canFind(validfs, fs)) {
                    if (debugflag && debuglevel > 0)
                        spdlog::debug("adding groupdefault <{}>", fs);
                    validfs.push_back(fs);
                    goto groupend;
                }
            }
        }
    }
groupend:

    // global default last
    if (debugflag && debuglevel > 0) {
        spdlog::debug("global.default_workspace={}", global.defaultWorkspace);
        spdlog::debug("hasAccess({}, {}, {})={}", user, groups, global.defaultWorkspace,
                      hasAccess(user, groups, global.defaultWorkspace, intent));
        spdlog::debug("canFind({}, {})={}", validfs, global.defaultWorkspace,
                      canFind(validfs, global.defaultWorkspace));
    }
    if ((global.defaultWorkspace != "") && hasAccess(user, groups, global.defaultWorkspace, intent) &&
        !canFind(validfs, global.defaultWorkspace)) {
        if (debugflag && debuglevel > 0)
            spdlog::debug("adding default_workspace <{}>", global.defaultWorkspace);
        validfs.push_back(global.defaultWorkspace);
    }

    // now all others with access
    for (auto const& [fs, val] : filesystems) {
        if (hasAccess(user, groups, fs, intent) && !canFind(validfs, fs)) {
            if (debugflag && debuglevel > 0)
                spdlog::debug("adding as having access <{}>", fs);
            validfs.push_back(fs);
        }
    }

    if (debugflag)
        spdlog::debug(" => valid filesystems {}", validfs);

    return validfs;
}

// get DB type for the fs
Database* Config::openDB(const string fs) const {
    if (traceflag)
        spdlog::trace("opendb {}", fs);
    // TODO: version check here to determine which DB to open

    // check for magic in DB entry, to avoid that DB is not existing and all workspaces
    // get wiped by accident, e.g. due to mouting problems if DB is not in same FS as workspaces
    if (cppfs::exists(cppfs::path(getFsConfig(fs).database) / ".ws_db_magic")) {
        auto magic = utils::getFirstLine(utils::getFileContents(getFsConfig(fs).database + "/.ws_db_magic"));
        if (magic != fs) {
            throw DatabaseException(fmt::format(
                "DB directory {} from fs {} does not contain .ws_db_magic with correct workspace name in it",
                getFsConfig(fs).database, fs));
        }
    } else {
        throw DatabaseException(
            fmt::format("DB directory {} from fs {} does not contain .ws_db_magic", getFsConfig(fs).database, fs));
    }

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
        spdlog::trace("getFsConfig({})", filesystem);
    try {
        return filesystems.at(filesystem);
    } catch (const std::out_of_range& e) {
        spdlog::error("no valid filesystem ({}) given in getFsConfig(), should not happen", filesystem);
        exit(-1); // should not be reached
    }
}
