#ifndef CONFIG_H
#define CONFIG_H

/*
 *  hpc-workspace-v2
 *
 *  config.h
 *
 * - deals with global configuration files
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
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "db.h"
#include "ws.h"

using namespace std;
namespace cppfs = std::filesystem;

using strings = std::vector<string>;

// global part of config
struct Global_config {
    string clustername;      // name of cluster for mails
    string smtphost;         // smtp host for sending mails
    string mail_from;        // sender for mails
    string defaultWorkspace; // workspace to use if several are allowed
    strings admins;          // people allowed to see all workspaces
    strings adminmail;       // mail addresses to alert in case of problems
    int maxduration;         // max duration user can choose
    int durationdefault;     // default duration
    int reminderdefault;     // when to send a reminder, 0 no default reminder
    int maxextensions;       // max extensions a user gets
    int dbuid;               // uid of DB user
    int dbgid;               // gid of DB user
    int deldirtimeout;       // timeout for directory deletion
};

// config of filesystem
struct Filesystem_config {
    string name;           // name of filesystem
    string comment;        // some notice for the user about this filesystem
    strings spaces;        // prefix path in filesystem for workspaces
    string spaceselection; // method to select from spaces list: random (default), uid, gid
    string deletedPath;    // subdirectory to move deleted workspaces to, relative path
    string database;       // path to workspace db for this filesystem
    strings groupdefault;  // groups having this filesystem as default
    strings userdefault;   // users having this filesytem as default
    strings user_acl;      // if present, users have to match ACL, user or +user grant access, -user denies
    strings group_acl;     // if present, users have to match ACL
    int keeptime;          // max time in days to keep deleted workspace
    int maxduration;       // max duration a user can choose for this filesystem
    int maxextensions;     // max extensiones a user can do for this filesystem
    // migration helpers
    bool allocatable; // is this filesystem allocatable? (or read only?)
    bool extendable;  // is this filesystem extendable? (or read only?)
    bool restorable;  // can a workspace be restored into this filesystem?
};
// #include "UserConfig.h"

// global config, global settings + workspaces
class Config {

  private:
    // global settings
    Global_config global;

    // Filesystems
    std::map<string, Filesystem_config> filesystems; // list of workspace filesystems

    // validation helpers
    bool isvalid;

  public:
    // read config from list of files or directories, in given order, stops after first existing file
    // (even if invalid!) but reads all fiels if a directory is given.
    Config(const std::vector<cppfs::path> filenames);
    // read config from string
    Config(const string configstring);

    // validate, public for tests, to be called after string constructor
    bool validate();

    // check if config is valid
    bool isValid() { return isvalid; };

    // check if user is an admin
    bool isAdmin(const string user) const;
    // get list of valid filesystems for user
    vector<string> validFilesystems(const string user, const vector<string> groups, const ws::intent intent) const;
    // check if given user can assess given filesystem with current config
    bool hasAccess(const string user, const std::vector<string> groups, const string filesystemm,
                   const ws::intent intent) const;
    // get list of all filesystems
    vector<string> Filesystems() const;

    // return DB handle of right version
    Database* openDB(const string fs) const;

    // get config of a filesystem
    Filesystem_config getFsConfig(const std::string filesystem) const;

    // return path to database for given filesystem
    string database(const string filesystem) const;
    // return path to deletedpath for given filesystem
    string deletedPath(const string filesystem) const;
    int reminderdefault() const { return global.reminderdefault; };
    int durationdefault() const { return global.durationdefault; };
    long dbuid() const { return global.dbuid; };
    long dbgid() const { return global.dbgid; };
    string clustername() const { return global.clustername; };
    int maxextensions() const { return global.maxextensions; };
    int maxduration() const { return global.maxduration; };
    string defaultworkspace() const { return global.defaultWorkspace; };
    int deldirtimeout() const { return global.deldirtimeout; };
    string mailfrom() const { return global.mail_from; };
    string smtphost() const { return global.smtphost; };
    vector<string> admins() const { return global.admins; };
    vector<string> adminmail() const { return global.adminmail; };

  private:
    // read config from YAML string
    void readYAML(string YAML);
};

// helper for std::find
// #define canFind(x, y) (std::find(x.begin(), x.end(), y) != x.end())
template <typename T1, typename T2> bool canFind(T1 x, T2 y) { return (std::find(x.begin(), x.end(), y) != x.end()); }

#endif
