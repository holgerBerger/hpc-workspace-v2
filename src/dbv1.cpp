/*
 *  hpc-workspace-v2
 *
 *  dbv1.cpp
 *
 *  - interface to v1 format database
 *    this is the DB format as written from legacy workspace++
 *
 *  c++ version of workspace utility
 *  a workspace is a temporary directory created in behalf of a user with a limited lifetime.
 *
 *  (c) Holger Berger 2021,2023,2024,2025
 *
 *  hpc-workspace-v2 is based on workspace by Holger Berger, Thomas Beisel and Martin Hecht
 *
 *  hpc-workspace-v2 is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public Lice//nse as published by
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
#include <cctype>
#include <filesystem>
#include <set>
#include <string>
#include <system_error>
#include <unistd.h>
#include <vector>

// use for speed, but needs testing // FIXME:
#define WS_RAPIDYAML_DB

#ifdef WS_RAPIDYAML_DB
    #define RYML_USE_ASSERT 0
    #include "c4/format.hpp" // IWYU pragma: keep
    #include "ryml.hpp"      // IWYU pragma: keep
    #include "ryml_std.hpp"  // IWYU pragma: keep
#else
    #include <yaml-cpp/yaml.h>
#endif

#include "dbv1.h"
#include "fmt/base.h"
#include "fmt/ranges.h" // IWYU pragma: keep
#include "user.h"
#include "utils.h"

#include <signal.h>
// for chown
#include <fcntl.h>
#include <sys/stat.h>
// for statfs
#ifdef __linux__
    #include <sys/vfs.h>
#elif __APPLE__
    #include <sys/mount.h>
    #include <sys/param.h>
#endif
// for getgrpnam
#include <grp.h>
#include <sys/types.h>

#include <syslog.h>

#ifndef WS_RAPIDYAML_DB
    #include <sstream>
#endif

#include "caps.h"

#include <gsl/pointers>

#include "spdlog/spdlog.h"

using namespace std;

// globals
extern bool debugflag;
extern bool traceflag;
extern Cap caps;

namespace cppfs = std::filesystem;

// validate a workspace ID before it is used as a DB entry filename (single path component).
// Rejects empty, ".", ".." and any ID containing '/': otherwise a crafted ID
// (e.g. via -u ../something in ws_allocate) escapes the database directory and lets a
// user direct privileged reads/writes at arbitrary paths. The rest of the charset is
// intentionally lenient: existing sites have non-POSIX (LDAP/AD) usernames in entry names.
static void checkID(const string& id) {
    if (id.empty() || id == "." || id == ".." || id.find('/') != string::npos) {
        throw DatabaseException(fmt::format("invalid workspace id <{}>", id));
    }
}

// create the workspace directory with the structure of this DB
string FilesystemDBV1::createWorkspace(const string name, const string user_option, const bool groupflag,
                                       const bool groupwritable, const string groupname) {
    string wsdir;

    std::string username = user::getUsername(); // current user

    int spaceid = 0;

    auto spaces = config->getFsConfig(fs).spaces;

    if (spaces.size() > 1) {
        auto spaceselection = config->getFsConfig(fs).spaceselection;
        if (debugflag) {
            spdlog::debug("spaceseletion for {} = {}", fs, spaceselection);
        }
        // select space according to spaceselection in config
        if (spaceselection == "random") {
            srand(time(NULL));
            spaceid = rand() % spaces.size();
        } else if (spaceselection == "uid") {
            spaceid = getuid() % spaces.size();
        } else if (spaceselection == "gid") {
            spaceid = getgid() % spaces.size();
        } else if (spaceselection == "mostspace") {
            spaceid = 0;
            fsblkcnt_t max_free_bytes = 0;
            for (size_t i = 0; i < spaces.size(); ++i) {
                struct statfs sfs;
                statfs(spaces[i].c_str(), &sfs);
                fsblkcnt_t free_bytes = sfs.f_bsize * sfs.f_bfree;
                if (free_bytes > max_free_bytes) {
                    max_free_bytes = free_bytes;
                    spaceid = i;
                }
            }
        }
        if (debugflag) {
            spdlog::debug("spaceid={}", spaceid);
        }
    }

    // determine name of workspace directory

    if (user_option.length() > 0 && (user_option != username) && (getuid() != 0)) {
        wsdir = spaces[spaceid] + "/" + username + "-" + name;
    } else { // we are root and can change owner!
        string randspace = spaces[spaceid];
        if (user_option.length() > 0 && (getuid() == 0)) {
            wsdir = randspace + "/" + user_option + "-" + name;
        } else {
            wsdir = randspace + "/" + username + "-" + name;
        }
    }

    auto db_uid = config->dbuid();
    // auto db_gid = config->dbgid();

    // make directory and change owner + permissions
    try {
        caps.raise_cap({CAP_DAC_OVERRIDE}, utils::SrcPos(__FILE__, __LINE__, __func__));
        mode_t oldmask = umask(077); // as we create intermediate directories, we better take care of umask!!
        cppfs::create_directories(wsdir);
        umask(oldmask);
        caps.lower_cap({CAP_DAC_OVERRIDE}, db_uid, utils::SrcPos(__FILE__, __LINE__, __func__));
    } catch (cppfs::filesystem_error const& e) {
        auto uid = getuid();
        auto euid = geteuid();
        caps.lower_cap({CAP_DAC_OVERRIDE}, db_uid, utils::SrcPos(__FILE__, __LINE__, __func__));
        spdlog::error("could not create workspace directory <{}>!", wsdir);
        if (debugflag) {
            spdlog::debug("error = {}", e.what());
            spdlog::debug("uid: {} euid: {}", uid, euid);
        }
        exit(-1); // FIXME: throw
    }

    uid_t tuid = getuid();
    gid_t tgid = getgid();

    if (user_option.length() > 0) {
        gsl::not_null<struct passwd*> pws = getpwnam(user_option.c_str());
        tuid = pws->pw_uid;
        tgid = pws->pw_gid;
    }

    if (groupname != "") {
        gsl::not_null<struct group*> grp = getgrnam(groupname.c_str());
        if (grp) {
            tgid = grp->gr_gid;
        }
    }

    caps.raise_cap({CAP_CHOWN}, utils::SrcPos(__FILE__, __LINE__, __func__));

    if (chown(wsdir.c_str(), tuid, tgid)) {
        caps.lower_cap({CAP_CHOWN}, db_uid, utils::SrcPos(__FILE__, __LINE__, __func__));
        spdlog::error("could not change owner of workspace!");
        unlink(wsdir.c_str());
        exit(-1); // FIXME: throw
    }
    caps.lower_cap({CAP_CHOWN}, db_uid, utils::SrcPos(__FILE__, __LINE__, __func__));

    caps.raise_cap({CAP_FOWNER}, utils::SrcPos(__FILE__, __LINE__, __func__));
    mode_t mode = S_IRUSR | S_IWUSR | S_IXUSR;

    // group workspaces can be read and listed by group
    if (groupflag) {
        // SPEC:CHANGE setuid bit is always set, for readable group workspaces as well
        mode |= S_IRGRP | S_IXGRP | S_ISGID;
    }
    // if a groupname is given make it writable as well
    if (groupwritable) {
        mode |= S_IWGRP;
    }
    if (chmod(wsdir.c_str(), mode)) {
        caps.lower_cap({CAP_FOWNER}, db_uid, utils::SrcPos(__FILE__, __LINE__, __func__));
        spdlog::error("could not change permissions of workspace!");
        unlink(wsdir.c_str());
        exit(-1); // FIXME: throw
    }
    caps.lower_cap({CAP_FOWNER}, db_uid, utils::SrcPos(__FILE__, __LINE__, __func__));

    return wsdir;
}

// create new DB entry
void FilesystemDBV1::createEntry(const WsID id, const string username, const string workspace, const long creation,
                                 const long expiration, const long reminder, const int extensions, const bool groupflag,
                                 const string group, const string mailaddress, const string comment) {

    DBEntryV1 entry(this, id, username, workspace, creation, expiration, reminder, extensions, groupflag, group,
                    mailaddress, comment);
    entry.writeEntry();
}

// get a list of ids of matching DB entries for a user
// groupworkspaces flag determines if content of DB entry has to be checked for correct group
//  unittest: yes
vector<WsID> FilesystemDBV1::matchPattern(const string pattern, const string user, const vector<string> groups,
                                          const bool deleted, const bool groupworkspaces) {
    if (traceflag)
        spdlog::trace("matchPattern(pattern={},user={},groups={},deleted={},groupworkspace={})", pattern, user, groups,
                      deleted, groupworkspaces);

    // list directory, this also reads YAML file in case of groupworkspaces
    auto listdir = [&groupworkspaces, &groups](const string pathname, const string filepattern) -> vector<string> {
        if (debugflag)
            spdlog::debug("listdir({},{})", pathname, filepattern);
        try {
            // in case of groupworkspace, read entry
            if (groupworkspaces) {
                auto filelist = utils::dirEntries(pathname, filepattern, false);
                vector<string> list;
                utils::HasGroupIntersection groupintersection(user::getUsername());

                for (auto const& f : filelist) {
                    // optimization: check if owner (from filename) has common groups with caller. if not, do not read
                    // only take shortcut if username does not contain - (->2 fields in ID)
                    auto parts = utils::splitString(f, '-');
                    if (parts.size() == 2 && !groupintersection.hasCommonGroups(parts[0]))
                        continue;

#ifndef WS_RAPIDYAML_DB
                    YAML::Node dbentry;
                    try {
                        dbentry = YAML::LoadFile((cppfs::path(pathname) / f).string().c_str());
                    } catch (const YAML::BadFile& e) {
                        spdlog::error("Could not read db entry {}: {}", f, e.what());
                    }

                    string group = "";
                    if (dbentry["group"]) {
                        group = dbentry["group"].as<string>();
                    }
#else
                    string filecontent = utils::getFileContents((cppfs::path(pathname) / f).string().c_str());
                    ryml::Tree dbentry = ryml::parse_in_place(ryml::to_substr(filecontent)); // FIXME: error check?

                    ryml::NodeRef node;
                    string group;
                    node = dbentry["group"];
                    if (node.has_val() && node.val() != "")
                        node >> group;
                    else
                        group = "";
#endif

                    if (canFind(groups, group)) {
                        list.push_back(f);
                    }
                }
                return list;
            } else { // simple file match, no group workspace
                return utils::dirEntries(pathname, filepattern, false);
            }
        } catch (cppfs::filesystem_error const& e) {
            throw DatabaseException(e.what());
        }
    };

    // this has to happen here, as other DB might have different patterns
    string filepattern;
    if (groupworkspaces)
        filepattern = fmt::format("*-{}", pattern);
    else
        filepattern = fmt::format("{}-{}", user, pattern);

    // scan filesystem
    if (deleted)
        return listdir(cppfs::path(config->database(fs)) / config->deletedPath(fs), filepattern);
    else
        return listdir(config->database(fs), filepattern);
}

// read entry
//  unittest: yes
std::unique_ptr<DBEntry> FilesystemDBV1::readEntry(const WsID id, const bool deleted) {
    if (traceflag)
        spdlog::trace("readEntry({},{})", id, deleted);
    checkID(id);
    std::unique_ptr<DBEntry> entry(new DBEntryV1(this));
    string filename;
    if (deleted)
        filename = cppfs::path(config->database(fs)) / config->deletedPath(fs) / id;
    else
        filename = cppfs::path(config->database(fs)) / id;
    entry->readFromFile(id, fs, filename);

    return entry;
}

// delete entry, ID can include timestamp of deleted workspace
void FilesystemDBV1::deleteEntry(const string wsid, const bool deleted) {
    cppfs::path dbentrypath;

    checkID(wsid);

    if (deleted) {
        dbentrypath = cppfs::path(config->getFsConfig(fs).database) / config->getFsConfig(fs).deletedPath / wsid;
    } else {
        dbentrypath = cppfs::path(config->getFsConfig(fs).database) / wsid;
    }
    if (debugflag)
        spdlog::debug("deleting DB entry {}", dbentrypath.string());

    try {
        cppfs::remove(dbentrypath);
    } catch (cppfs::filesystem_error const& ex) {
        throw(DatabaseException(ex.code().message()));
    }
}

// constructor to make new entry to write out
DBEntryV1::DBEntryV1(FilesystemDBV1* pdb, const WsID _id, const string _username, const string _workspace,
                     const long _creation, const long _expiration, const long _reminder, const int _extensions,
                     const bool _groupflag, const string _group, const string _mailaddress, const string _comment)
    : parent_db(pdb), id(_id), username(_username), workspace(_workspace), creation(_creation), expiration(_expiration),
      reminder(_reminder), extensions(_extensions), groupflag(_groupflag), group(_group), mailaddress(_mailaddress),
      comment(_comment) {
    checkID(id);
    dbfilepath = pdb->getconfig()->getFsConfig(pdb->getfs()).database + "/" + id;
    // init extra internals here to avoid problems in release builds
    released = 0;
    expired = 0;
    acctcode = "";
}

// read db entry from yaml file
//  unittest: yes
void DBEntryV1::readFromFile(const WsID id, const string filesystem, const string filename) {
    if (traceflag)
        spdlog::trace("readFromFile({},{},{})", id, filesystem, filename);

    this->id = id;
    this->filesystem = filesystem;
    this->dbfilepath = filename; // store location if db entry for later writing

    std::string filecontent = utils::getFileContents(filename.c_str());
    if (filecontent == "") {
        throw DatabaseException(fmt::format("could not read file <{}>", filename));
    }

    try {
        readFromString(filecontent);
    } catch (const std::exception& e) {
        throw DatabaseException(fmt::format("while reading file <{}>\n{}", filename, e.what()));
    }

    /* NOT ENABLED TO AVOID LEAKING DETAILS
    if (debugflag) {
        spdlog::debug("  id={} creation={} released={} expiration={} reminder={} workspace={} extensions={} "
                      "mailaddress={} comment={} group={}",
                      id, creation, released, expiration, reminder, workspace, extensions, mailaddress, comment, group);
    }
    */
}

#ifndef WS_RAPIDYAML_DB
// use yamlcpp

// read db entry from yaml file
//  unittest: yes
void DBEntryV1::readFromString(std::string str) {
    if (traceflag)
        spdlog::trace("readFromString_YAMLCPP");

    YAML::Node dbentry;
    try {
        dbentry = YAML::Load(str);
    } catch (const YAML::BadFile& e) {
        throw DatabaseException("could not read db entry");
    }

    if (dbentry.size() == 0) {
        fmt::println(">>>", str);
        throw DatabaseException("Invalid DB entry! Empty file?");
    }

    dbversion = dbentry["dbversion"] ? dbentry["dbversion"].as<int>() : 0; // 0 = legacy
    workspace = dbentry["workspace"] ? dbentry["workspace"].as<string>() : "";
    // `user` field was added later; legacy entries may lack it (empty = fall back to heuristic)
    username = dbentry["user"] ? dbentry["user"].as<string>() : "";
    if (dbentry["creation"]) {
        creation = dbentry["creation"].as<long>();
    } else {
        creation = utils::getFileTimeAsLong(cppfs::path(
            workspace)); // use filesystem creation time for DB entries lacking the creation time, as V1 does
    }
    released = dbentry["released"] ? dbentry["released"].as<long>() : 0;
    expiration = dbentry["expiration"] ? dbentry["expiration"].as<long>() : 0;
    expired = dbentry["expired"] ? dbentry["expired"].as<long>() : 0;
    reminder = dbentry["reminder"] ? dbentry["reminder"].as<long>() : 0;
    extensions = dbentry["extensions"] ? dbentry["extensions"].as<int>() : 0;
    mailaddress = dbentry["mailaddress"] ? dbentry["mailaddress"].as<string>() : "";
    comment = dbentry["comment"] ? dbentry["comment"].as<string>() : "";
    group = dbentry["group"] ? dbentry["group"].as<string>() : "";
    if (group != "")
        groupflag = true;
    else
        groupflag = false;
    acctcode = dbentry["acctcode"] ? dbentry["acctcode"].as<string>() : "";
}

#else
// use rapidyaml

// read db entry from yaml string
//  unittest: yes
void DBEntryV1::readFromString(std::string str) {
    if (traceflag)
        spdlog::trace("readFromString_RAPIDYAML");

    // Set up temporary error handler for parsing
    ryml::Callbacks const prev_callbacks = ryml::get_callbacks();
    ryml::Callbacks callbacks = {};

    // Error handler that throws DatabaseException on parse errors
    callbacks.m_error_parse = [](ryml::csubstr msg, ryml::ErrorDataParse const&, void*) {
        throw DatabaseException("YAML parse error: " + std::string(msg.str, msg.len));
    };
    callbacks.set_user_data(nullptr);

    // Also set basic error handler as fallback
    callbacks.m_error_basic = [](ryml::csubstr msg, ryml::ErrorDataBasic const&, void*) {
        throw DatabaseException("YAML error: " + std::string(msg.str, msg.len));
    };

    ryml::set_callbacks(callbacks);

    ryml::Tree dbentry = ryml::parse_in_place(ryml::to_substr(str));

    // Restore previous callbacks
    ryml::set_callbacks(prev_callbacks);

    // error check, see if the file looks like yaml and is a map
    ryml::NodeRef node;
    auto root = dbentry.crootref();
    if (!root.is_map()) {
        throw DatabaseException("Invalid DB entry! Empty file?");
    }

    node = dbentry["dbversion"];
    if (node.has_val())
        node >> dbversion;
    else
        dbversion = 0; // 0 = legacy
    node = dbentry["workspace"];
    if (node.has_val() && node.val() != "")
        node >> workspace;
    else
        workspace = "";
    // `user` field was added later; legacy entries may lack it (empty = fall back to heuristic)
    node = dbentry["user"];
    if (node.has_val() && node.val() != "")
        node >> username;
    else
        username = "";
    node = dbentry["creation"];
    if (node.has_val()) {
        node >> creation;
    } else {
        creation = utils::getFileTimeAsLong(cppfs::path(
            workspace)); // use filesystem creation time for DB entries lacking the creation time, as V1 does
    }
    node = dbentry["released"];
    if (node.has_val())
        node >> released;
    else
        released = 0;
    node = dbentry["expiration"];
    if (node.has_val())
        node >> expiration;
    else
        expiration = 0;
    node = dbentry["expired"];
    if (node.has_val())
        node >> expired;
    else
        expired = 0;
    node = dbentry["reminder"];
    if (node.has_val())
        node >> reminder;
    else
        reminder = 0;
    node = dbentry["extensions"];
    if (node.has_val())
        node >> extensions;
    else
        extensions = 0;
    node = dbentry["mailaddress"];
    if (node.has_val() && node.val() != "")
        node >> mailaddress;
    else
        mailaddress = "";
    node = dbentry["comment"];
    if (node.has_val() && node.val() != "")
        node >> comment;
    else
        comment = "";
    node = dbentry["group"];
    if (node.has_val() && node.val() != "") {
        groupflag = true;
        node >> group;
    } else {
        groupflag = false;
        group = "";
    }
    node = dbentry["acctcode"];
    if (node.has_val() && node.val() != "")
        node >> acctcode;
    else
        acctcode = "";
}

#endif

// Use extension or update content of entry
//  unittest: yes
void DBEntryV1::useExtension(const long _expiration, const string _mailaddress, const int _reminder,
                             const string _comment) {
    if (traceflag)
        spdlog::trace("useExtension(expiration={},mailaddress={},reminder={},comment={})\n");
    if (_mailaddress != "")
        mailaddress = _mailaddress;
    if (_reminder != 0)
        reminder = _reminder;
    if (_comment != "")
        comment = _comment;

    // if root does this, we do not use an extension
    if ((getuid() != 0) && (_expiration != -1) && (_expiration > expiration)) {
        extensions--;
    }
    if ((extensions < 0) && (getuid() != 0)) {
        throw DatabaseException("no more extensions!");
    }
    if (_expiration != -1) {
        expiration = _expiration;
    }
    writeEntry();
}

long DBEntryV1::getRemaining() const { return expiration - time(0L); };

int DBEntryV1::getExtension() const {
    // if(traceflag) spdlog::trace("getExtension()\n");
    // if(debugflag) fmt::print(stderr, "Debug  : extensions={}\n", extensions);
    return extensions;
}

string DBEntryV1::getId() const { return id; }

// Owner of this workspace. For legacy entries created before the `user` field existed,
// falls back to the dash heuristic (best effort: fails if user or name contain dashes).
string DBEntryV1::getUsername() const {
    if (!username.empty()) {
        return username;
    }
    return utils::getOwner(id);
}

long DBEntryV1::getCreation() const { return creation; }

string DBEntryV1::getWSPath() const { return workspace; };

string DBEntryV1::getMailaddress() const { return mailaddress; }

string DBEntryV1::getComment() const { return comment; }

long DBEntryV1::getExpired() const { return expired; }

long DBEntryV1::getExpiration() const { return expiration; }

long DBEntryV1::getReleaseTime() const { return released; }

string DBEntryV1::getFilesystem() const { return filesystem; }

long DBEntryV1::getReminder() const { return reminder; }

string DBEntryV1::getGroup() const { return group; }

void DBEntryV1::setGroup(const string& g) {
    group = g;
    groupflag = !g.empty();
}

// change expiration time
void DBEntryV1::setExpiration(const time_t timestamp) { expiration = timestamp; }

// change expired time
void DBEntryV1::setExpired(const time_t timestamp) { expired = timestamp; }

// split an ID of an entry in the deleted area "<base>-<ts>" into base and ts
// returns false if the ID does not have that shape
static bool splitExpiredId(const string& id, string& base, time_t& ts) {
    auto dash = id.rfind('-');
    if (dash == string::npos || dash == 0 || dash + 1 >= id.size())
        return false;
    auto suffix = id.substr(dash + 1);
    if (!std::all_of(suffix.begin(), suffix.end(), [](unsigned char c) { return std::isdigit(c); }))
        return false;
    try {
        ts = std::stoll(suffix);
    } catch (const std::exception&) {
        return false;
    }
    base = id.substr(0, dash);
    return true;
}

// check if something (file, directory, dangling symlink) exists under that name
// throws if this can not be determined, to never overwrite something by accident
static bool nameTaken(const cppfs::path& p) {
    std::error_code ec;
    auto st = cppfs::symlink_status(p, ec);
    if (ec && st.type() != cppfs::file_type::not_found) {
        throw DatabaseException(fmt::format("can not check existence of {}: {}", p.string(), ec.message()));
    }
    return st.type() != cppfs::file_type::not_found;
}

// rename a DB entry file as DB user (as release() does), throws DatabaseException on failure
static void renameDBFile(const Config* config, const cppfs::path& from, const cppfs::path& to) {
    caps.raise_cap({CAP_FOWNER, CAP_DAC_OVERRIDE}, utils::SrcPos(__FILE__, __LINE__, __func__));

    if (caps.isSetuid()) {
        // for filesystems with root_squash, we need to be DB user here
        if (setegid(config->dbgid()) || seteuid(config->dbuid())) {
            caps.lower_cap({CAP_DAC_OVERRIDE, CAP_FOWNER}, config->dbuid(),
                           utils::SrcPos(__FILE__, __LINE__, __func__));
            throw DatabaseException("can not seteuid or setgid. Bad installation?");
        }
    }

    if (debugflag)
        spdlog::debug("rename({}, {})", from.string(), to.string());
    std::error_code ec;
    cppfs::rename(from, to, ec);

    caps.lower_cap({CAP_DAC_OVERRIDE, CAP_FOWNER}, config->dbuid(), utils::SrcPos(__FILE__, __LINE__, __func__));

    if (ec) {
        throw DatabaseException(
            fmt::format("could not rename DB entry {} -> {}: {}", from.string(), to.string(), ec.message()));
    }
}

// rename a workspace directory (as ws_release does), returns error code
static std::error_code renameWorkspaceDir(const Config* config, const cppfs::path& from, const cppfs::path& to) {
    caps.raise_cap({CAP_DAC_OVERRIDE}, utils::SrcPos(__FILE__, __LINE__, __func__));
    if (debugflag)
        spdlog::debug("rename({}, {})", from.string(), to.string());
    std::error_code ec;
    cppfs::rename(from, to, ec); // same directory, can not be cross device
    caps.lower_cap({CAP_DAC_OVERRIDE}, config->dbuid(), utils::SrcPos(__FILE__, __LINE__, __func__));
    return ec;
}

// collision free ID "<base>-<ts>" with smallest ts >= boundary for an entry in the deleted area,
// checks DB entries and workspace directories in the deleted area and names reserved by the caller
WsID DBEntryV1::expiredIdFor(const time_t boundary, const std::set<WsID>& reserved) const {
    if (traceflag)
        spdlog::trace("expiredIdFor({})", boundary);

    string base;
    time_t ts;
    if (!splitExpiredId(id, base, ts)) {
        throw DatabaseException(fmt::format("id {} has no timestamp suffix to replace", id));
    }
    if (boundary <= 0) {
        throw DatabaseException(fmt::format("refusing to rename entry {} to implausible timestamp {}", id, boundary));
    }

    auto wsconfig = parent_db->getconfig()->getFsConfig(filesystem);
    auto db_deleted_dir = cppfs::path(wsconfig.database) / wsconfig.deletedPath;
    auto ws_deleted_dir = cppfs::path(workspace).remove_filename() / wsconfig.deletedPath;

    // the timestamp is a computed date, not "now", so waiting would not help: count up instead
    const int maxtries = 1000;
    for (int i = 0; i < maxtries; i++) {
        WsID candidate = fmt::format("{}-{}", base, boundary + i);
        if (candidate == id)
            return id; // already carries the right (or a sufficiently close) name
        if (reserved.count(candidate))
            continue;
        if (nameTaken(db_deleted_dir / candidate) || nameTaken(ws_deleted_dir / candidate))
            continue;
        return candidate;
    }
    throw DatabaseException(fmt::format("no free name found for {} after {} tries", id, maxtries));
}

// rename DB entry and workspace directory of an entry from the deleted area to new_id.
// DB entry first, workspace second (as ws_expirer does it). Never overwrites, and rolls back the
// DB rename if the workspace can not be moved, as a DB entry without workspace directory is
// harmless, but a workspace directory without DB entry is removed as stray by ws_expirer.
void DBEntryV1::renameAsExpired(const WsID& new_id) {
    if (traceflag)
        spdlog::trace("renameAsExpired({})", new_id);

    // released entries carry the release time in their name, not an expired time
    if (released > 1000000000L) {
        throw DatabaseException(fmt::format("entry {} was released by user, name carries release time", id));
    }
    string base, new_base;
    time_t ts, new_ts;
    if (!splitExpiredId(id, base, ts)) {
        throw DatabaseException(fmt::format("id {} has no timestamp suffix to replace", id));
    }
    if (!splitExpiredId(new_id, new_base, new_ts) || new_base != base || new_ts <= 0) {
        throw DatabaseException(fmt::format("invalid new id {} for entry {}", new_id, id));
    }
    if (new_id == id)
        return;

    const Config* config = parent_db->getconfig();
    auto wsconfig = config->getFsConfig(filesystem);
    auto ws_deleted_dir = cppfs::path(workspace).remove_filename() / wsconfig.deletedPath;

    cppfs::path db_from = dbfilepath;
    cppfs::path db_to = cppfs::path(wsconfig.database) / wsconfig.deletedPath / new_id;
    cppfs::path ws_from = ws_deleted_dir / id;
    cppfs::path ws_to = ws_deleted_dir / new_id;

    // std::filesystem::rename replaces existing files and empty directories, never do that
    if (nameTaken(db_to)) {
        throw DatabaseException(fmt::format("DB entry {} exists already, not overwriting", db_to.string()));
    }
    if (nameTaken(ws_to)) {
        throw DatabaseException(fmt::format("{} exists already, not overwriting", ws_to.string()));
    }

    bool ws_present = nameTaken(ws_from);
    if (!ws_present) {
        spdlog::warn("workspace directory {} not found, renaming DB entry {} only", ws_from.string(), id);
    }

    // DB entry first
    renameDBFile(config, db_from, db_to);
    WsID old_id = id;
    dbfilepath = db_to.string(); // entry knows now the new name, for remove and writeEntry
    id = new_id;

    // workspace second
    if (ws_present) {
        auto ec = renameWorkspaceDir(config, ws_from, ws_to);
        if (ec) {
            // roll back, otherwise the workspace directory would be removed as stray by ws_expirer
            try {
                renameDBFile(config, db_to, db_from);
            } catch (const DatabaseException& e) {
                throw DatabaseException(fmt::format(
                    "could not move workspace {} -> {} ({}), and rollback of DB entry failed ({}). "
                    "REPAIR MANUALLY: rename {} to {}, otherwise ws_expirer removes the workspace as stray!",
                    ws_from.string(), ws_to.string(), ec.message(), e.what(), db_to.string(), db_from.string()));
            }
            dbfilepath = db_from.string();
            id = old_id;
            throw DatabaseException(fmt::format("could not move workspace {} -> {} ({}), DB entry rename rolled back",
                                                ws_from.string(), ws_to.string(), ec.message()));
        }
    }
}

// change release date (mark as released and not expired)
// write DB entry
// move DB entry to releases entries
void DBEntryV1::release(time_t& timestamp_time) {
    if (traceflag)
        spdlog::trace("release({})", timestamp_time);

    // TODO: is this ctrl-c save? should it be ignored for all of this?
    // probably ok, even if there is some partial state, ws_expirer would deal with it

    string timestamp = fmt::format("{}", timestamp_time);

    released = time(NULL); // now
    writeEntry();

    auto wsconfig = parent_db->getconfig()->getFsConfig(filesystem);
    cppfs::path dbtarget = cppfs::path(wsconfig.database) / cppfs::path(wsconfig.deletedPath) /
                           cppfs::path(fmt::format("{}-{}", id, timestamp));

    if (debugflag)
        spdlog::debug("dbtarget={}", dbtarget.string());

    caps.raise_cap({CAP_FOWNER, CAP_DAC_OVERRIDE}, utils::SrcPos(__FILE__, __LINE__, __func__));

    if (caps.isSetuid()) {
        // for filesystem with root_squash, we need to be DB user here
        if (setegid(parent_db->getconfig()->dbgid()) || seteuid(parent_db->getconfig()->dbuid())) {
            caps.lower_cap({CAP_DAC_OVERRIDE, CAP_FOWNER}, parent_db->getconfig()->dbuid(),
                           utils::SrcPos(__FILE__, __LINE__, __func__));
            throw DatabaseException("can not seteuid or setgid. Bad installation?");
        }
    }

    // check if target exists, as std::filesystem::rename does delete target if it exists
    // in case of collision, delay and wait for 1s resolution (but increment to avoid glitches)
    if (cppfs::exists(dbtarget)) {
        spdlog::info("delaying to avoid name collision");
        sleep(1);
        timestamp_time++;
        timestamp = fmt::format("{}", timestamp_time);
        // create name again with new timestamp
        dbtarget = cppfs::path(wsconfig.database) / cppfs::path(wsconfig.deletedPath) /
                   cppfs::path(fmt::format("{}-{}", id, timestamp));
    }

    // do the rename with collision free name
    try {
        if (debugflag)
            spdlog::debug("rename({}, {})", dbfilepath, dbtarget.string());
        cppfs::rename(dbfilepath, dbtarget);
        dbfilepath = dbtarget.string(); // entry knows now the new name, for remove, but is not persistent
    } catch (const std::filesystem::filesystem_error& e) {
        auto euid = geteuid();
        caps.lower_cap({CAP_DAC_OVERRIDE, CAP_FOWNER}, parent_db->getconfig()->dbuid(),
                       utils::SrcPos(__FILE__, __LINE__, __func__));
        if (debugflag)
            spdlog::error("{} (euid={})", e.what(), euid);
        throw DatabaseException("database entry could not be deleted!");
    }

    caps.lower_cap({CAP_DAC_OVERRIDE, CAP_FOWNER}, parent_db->getconfig()->dbuid(),
                   utils::SrcPos(__FILE__, __LINE__, __func__));
}

// set expired (not released)
// SPEC: can be called by root only!
// SPEC: does not work on root_squash!
// remarkthis can be called as root or user in user test mode
// Never overwrites an existing deleted entry: on a name collision the timestamp is bumped to the
// next free one and returned, so the caller can move the workspace directory to the same name.
std::string DBEntryV1::expire(const std::string timestamp) {
    if (traceflag)
        spdlog::trace("expire({})", timestamp);

    if (timestamp.empty() ||
        !std::all_of(timestamp.begin(), timestamp.end(), [](unsigned char c) { return std::isdigit(c); })) {
        throw DatabaseException(fmt::format("refusing to expire {}: invalid timestamp {}", id, timestamp));
    }
    time_t base_ts;
    try {
        base_ts = std::stol(timestamp);
    } catch (const std::exception&) {
        throw DatabaseException(fmt::format("refusing to expire {}: timestamp {} out of range", id, timestamp));
    }

    auto wsconfig = parent_db->getconfig()->getFsConfig(filesystem);
    auto db_deleted_dir = cppfs::path(wsconfig.database) / wsconfig.deletedPath;
    auto ws_deleted_dir = cppfs::path(workspace).remove_filename() / wsconfig.deletedPath;

    // Find a free "<id>-<ts>" name in both deleted areas before touching anything, so a failed
    // lookup leaves the live entry untouched. Collisions happen when two expirer runs rename into
    // the same second (re-run, clock step); unlike expiredIdFor the timestamp here is "now", but
    // waiting is still not an option within one run, so count up. Check and rename are not atomic,
    // consistent with the other guards in this file.
    const int maxtries = 1000;
    std::string used_ts;
    cppfs::path dbtarget;
    for (int i = 0; i < maxtries; i++) {
        std::string candidate_ts = std::to_string(base_ts + i);
        WsID candidate = fmt::format("{}-{}", id, candidate_ts);
        if (!nameTaken(db_deleted_dir / candidate) && !nameTaken(ws_deleted_dir / candidate)) {
            used_ts = candidate_ts;
            dbtarget = db_deleted_dir / candidate;
            break;
        }
    }
    if (used_ts.empty()) {
        throw DatabaseException(fmt::format("no free name found to expire {} after {} tries", id, maxtries));
    }
    if (used_ts != timestamp)
        spdlog::info("   name {}-{} taken, expiring as {}-{} instead", id, timestamp, id, used_ts);

    if (debugflag)
        spdlog::debug("dbtarget={}", dbtarget.string());

    // DB part

    // update expired entry so we can later see when this was expired by this method
    expired = time(0L); // insteaf of making long from string again, just get time again as in caller
    writeEntry(true); // the option makes sure the writeEntry does not drop privileges, this is important for ws_expirer
                      // which is run as root or user but has to capabilities to drop

    // filesystem part
    try {
        if (debugflag)
            spdlog::debug("rename({}, {})", dbfilepath, dbtarget.string());
        cppfs::rename(dbfilepath, dbtarget);
        dbfilepath = dbtarget.string(); // entry knows now the new name, for remove, but is not persistent
    } catch (const std::filesystem::filesystem_error& e) {
        if (debugflag)
            spdlog::error("{}", e.what());
        throw DatabaseException("database entry could not be deleted!");
    }

    return used_ts;
}

// remove DB entry
void DBEntryV1::remove() {
    caps.raise_cap({CAP_DAC_OVERRIDE}, utils::SrcPos(__FILE__, __LINE__, __func__));

    if (caps.isSetuid()) {
        // for filesystem with root_squash, we need to be DB user here
        if (setegid(getConfig()->dbgid()) || seteuid(getConfig()->dbuid())) {
            spdlog::error("can not setuid, bad installation?");
        }
    }

    if (debugflag)
        spdlog::debug("deleting db entry file {}", dbfilepath);

    cppfs::remove(dbfilepath);

    caps.lower_cap({CAP_DAC_OVERRIDE}, getConfig()->dbuid(), utils::SrcPos(__FILE__, __LINE__, __func__));

    syslog(LOG_INFO, "removed db entry <%s> for user <%s>.", id.c_str(), user::getUsername().c_str());
}

// write data to file
//  unittest: yes
void DBEntryV1::writeEntry(const bool calledasroot) {
    if (traceflag)
        spdlog::trace("writeEntry()");
    int perm;
#ifndef WS_RAPIDYAML_DB
    YAML::Node entry;
    entry["workspace"] = workspace;
    if (!username.empty()) {
        // exact owner, avoids parsing the (ambiguous) id; omitted for legacy entries we read
        // without a `user` field, so a heuristic guess is never persisted into the DB
        entry["user"] = username;
    }
    entry["creation"] = creation;
    entry["expiration"] = expiration;
    if (expired > 0) {
        entry["expired"] = expired;
    }
    entry["extensions"] = extensions;
    entry["reminder"] = reminder;
    entry["mailaddress"] = mailaddress;
    if (groupflag && group.length() > 0) {
        entry["group"] = group;
    }
    if (released > 0) {
        entry["released"] = released;
    }
    entry["comment"] = comment;
    entry["acctcode"] = acctcode;
#else
    ryml::Tree tree;
    ryml::NodeRef root = tree.rootref();
    root |= ryml::MAP; // mark root as a MAP
    root["workspace"] << workspace;
    if (!username.empty()) {
        // exact owner, avoids parsing the (ambiguous) id; omitted for legacy entries we read
        // without a `user` field, so a heuristic guess is never persisted into the DB
        root["user"] << username;
    }
    root["creation"] << creation;
    root["expiration"] << expiration;
    if (expired > 0) {
        root["expired"] << expired;
    }
    root["extensions"] << extensions;
    root["acctcode"] << acctcode;
    root["reminder"] << reminder;
    root["mailaddress"] << mailaddress;
    if (groupflag && group.length() > 0) {
        root["group"] << group;
    }
    if (released > 0) {
        root["released"] << released;
    }
    root["comment"] << comment;

    std::string entry = ryml::emitrs_yaml<std::string>(tree);
#endif

    // serialize the entry to a plain string so the write below can go through a raw fd
#ifndef WS_RAPIDYAML_DB
    std::ostringstream entrystream;
    entrystream << entry;
    const std::string entrytext = entrystream.str();
#else
    const std::string& entrytext = entry;
#endif

    // suppress ctrl-c to prevent broken DB entries when FS is hanging and user gets nervous
    void (*old_sigint)(int) = signal(SIGINT, SIG_IGN);
    // restore the previous handler on every exit path, including exceptions escaping this
    // function (a leaked SIG_IGN would make the long-running caller un-killable by Ctrl-C)
    struct SigIntGuard {
        void (*prev)(int);
        ~SigIntGuard() { signal(SIGINT, prev); }
    } sigint_guard{old_sigint};

    if (!calledasroot)
        caps.raise_cap({CAP_DAC_OVERRIDE},
                       utils::SrcPos(__FILE__, __LINE__, __func__)); // === Section with raised capabuility START ====

    uid_t dbuid = 0;
    gid_t dbgid = 0;

    dbuid = parent_db->getconfig()->dbuid();
    dbgid = parent_db->getconfig()->dbgid();

    if (user::isSetuid()) {
        // for filesystem with root_squash, we need to be DB user here
        if (debugflag)
            spdlog::debug("isSetuid -> dbuid={}, dbgid={}", dbuid, dbgid);

        if (setegid(dbgid) || seteuid(dbuid)) {
            spdlog::error("can not seteuid or setgid. Bad installation?");
            signal(SIGINT, old_sigint); // exit() does not unwind, so the guard does not run
            exit(-1);
        }

        if (debugflag)
            spdlog::debug("isSetuid -> euid={}, egid={}", geteuid(), getegid());
    }

    // write entry to DB file, overwrite existing contents <<<< this is the critical write operation
    // if the write fails, the entry is not saved and the user is notified
    // TODO: might be better to rename the file instead of overwriting, try to write to a temp file first
    //       and rename it afterwards, or rename the old file back if it fails, or write to a new file and rename
    //       it to the old name.
    //       Question is:
    //       - under which circumstances should we rename the old file back if the write fails?
    //         there is not even always a file at the first place.
    //       - what is a good name/location for the backup file/or the temp file? risk of collisions?
    if (group.length() > 0) {
        // for group workspaces, we set the x-bit
        perm = 0744;
    } else {
        perm = 0644;
    }

    // Open the entry file directly instead of via ofstream: O_NOFOLLOW refuses to follow a
    // symlink planted at the entry path (a privileged process would otherwise truncate and
    // overwrite an arbitrary target file), and fchmod/fchown below act on this fd, so the
    // path is never re-resolved after the open. mode&~umask applies at creation, fchmod
    // corrects it on the fd regardless of umask before the fd becomes visible as final.
    int fd = open(dbfilepath.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW | O_CLOEXEC, perm);
    bool writeok = fd != -1;
    if (!writeok) {
        spdlog::error("could not open DB file <{}>: {}! Database entry not saved!", dbfilepath, std::strerror(errno));
    } else {
        const char* buf = entrytext.data();
        size_t left = entrytext.size();
        while (left > 0) {
            ssize_t written = write(fd, buf, left);
            if (written == -1) {
                if (errno == EINTR)
                    continue;
                break;
            }
            buf += written;
            left -= static_cast<size_t>(written);
        }
        writeok = left == 0;
        if (!writeok) {
            spdlog::error("could not write DB file! Please check if the outcome is as expected, "
                          "you might have to make a backup of the workspace to prevent loss of data!");
            if (debugflag)
                spdlog::debug("{}", std::strerror(errno));
        }
    }

    if (!calledasroot)
        caps.raise_cap({CAP_FOWNER}, utils::SrcPos(__FILE__, __LINE__, __func__));
    if (writeok && fchmod(fd, perm) != 0) {
        spdlog::error("could not change permissions of database entry");
        if (debugflag)
            spdlog::error("{}", std::strerror(errno));
    }

    if (!calledasroot)
        caps.lower_cap({CAP_FOWNER, CAP_DAC_OVERRIDE}, dbuid, utils::SrcPos(__FILE__, __LINE__, __func__));

    // if we have caps, we have to change owner, in setuid mode we created it as correct user
    if (caps.hasCaps()) {
        if (!calledasroot)
            caps.raise_cap({CAP_CHOWN}, utils::SrcPos(__FILE__, __LINE__, __func__));
        if (writeok && fchown(fd, dbuid, dbgid)) {
            caps.lower_cap({CAP_CHOWN}, dbuid, utils::SrcPos(__FILE__, __LINE__, __func__));
            spdlog::error("could not change owner of database entry.");
        }
        if (!calledasroot)
            caps.lower_cap({CAP_CHOWN}, dbuid, utils::SrcPos(__FILE__, __LINE__, __func__));
    }

    if (fd != -1)
        close(fd);
}

// return config of parent DB
const Config* DBEntryV1::getConfig() const { return parent_db->getconfig(); }

namespace utils {
std::string getDBYamlReader() {
#ifdef WS_RAPIDYAML_DB
    return "RapidYAML";
#else
    return "yaml-cpp";
#endif
}
} // namespace utils
