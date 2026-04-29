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

#include <filesystem>
#include <string>
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

#include <fstream>

#include "caps.h"

#include <gsl/pointers>

#include "spdlog/spdlog.h"

using namespace std;

// globals
extern bool debugflag;
extern bool traceflag;
extern Cap caps;

namespace cppfs = std::filesystem;

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
void FilesystemDBV1::createEntry(const WsID id, const string workspace, const long creation, const long expiration,
                                 const long reminder, const int extensions, const bool groupflag, const string group,
                                 const string mailaddress, const string comment) {

    DBEntryV1 entry(this, id, workspace, creation, expiration, reminder, extensions, groupflag, group, mailaddress,
                    comment);
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
DBEntryV1::DBEntryV1(FilesystemDBV1* pdb, const WsID _id, const string _workspace, const long _creation,
                     const long _expiration, const long _reminder, const int _extensions, const bool _groupflag,
                     const string _group, const string _mailaddress, const string _comment)
    : parent_db(pdb), id(_id), workspace(_workspace), creation(_creation), expiration(_expiration), reminder(_reminder),
      extensions(_extensions), groupflag(_groupflag), group(_group), mailaddress(_mailaddress), comment(_comment) {
    dbfilepath = pdb->getconfig()->getFsConfig(pdb->getfs()).database + "/" + id;
    // init extra internals here to avoid problems in release builds
    released = 0;
    expired = 0;
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
    creation = dbentry["creation"] ? dbentry["creation"].as<long>()
                                   : 0; // FIXME: c++ tool does not write this field, but takes from stat
    released = dbentry["released"] ? dbentry["released"].as<long>() : 0;
    expiration = dbentry["expiration"] ? dbentry["expiration"].as<long>() : 0;
    expired = dbentry["expired"] ? dbentry["expired"].as<long>() : 0;
    reminder = dbentry["reminder"] ? dbentry["reminder"].as<long>() : 0;
    workspace = dbentry["workspace"] ? dbentry["workspace"].as<string>() : "";
    extensions = dbentry["extensions"] ? dbentry["extensions"].as<int>() : 0;
    mailaddress = dbentry["mailaddress"] ? dbentry["mailaddress"].as<string>() : "";
    comment = dbentry["comment"] ? dbentry["comment"].as<string>() : "";
    group = dbentry["group"] ? dbentry["group"].as<string>() : "";
    if (group != "")
        groupflag = true;
    else
        groupflag = false;
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
    node = dbentry["creation"];
    if (node.has_val())
        node >> creation;
    else
        creation = 0; // FIXME: c++ tool does not write this field, but takes from stat
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
    node = dbentry["workspace"];
    if (node.has_val() && node.val() != "")
        node >> workspace;
    else
        workspace = "";
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

// change expiration time
void DBEntryV1::setExpiration(const time_t timestamp) { expiration = timestamp; }

// change expired time
void DBEntryV1::setExpired(const time_t timestamp) { expired = timestamp; }

// change release date (mark as released and not expired)
// write DB entry
// move DB entry to releases entries
void DBEntryV1::release(time_t& timestamp_time) {

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
        caps.lower_cap({CAP_DAC_OVERRIDE, CAP_FOWNER}, parent_db->getconfig()->dbuid(),
                       utils::SrcPos(__FILE__, __LINE__, __func__));
        if (debugflag)
            spdlog::error("{}", e.what());
        throw DatabaseException("database entry could not be deleted!");
    }

    caps.lower_cap({CAP_DAC_OVERRIDE, CAP_FOWNER}, parent_db->getconfig()->dbuid(),
                   utils::SrcPos(__FILE__, __LINE__, __func__));
}

// set expired (not released)
// SPEC: can be called by root only!
// SPEC: does not work on root_squash!
void DBEntryV1::expire(const std::string timestamp) {
    auto wsconfig = parent_db->getconfig()->getFsConfig(filesystem);
    cppfs::path dbtarget = cppfs::path(wsconfig.database) / cppfs::path(wsconfig.deletedPath) /
                           cppfs::path(fmt::format("{}-{}", id, timestamp));

    if (debugflag)
        spdlog::debug("dbtarget={}", dbtarget.string());

    // DB part

    // update expired entry so we can later see when this was expired by this method
    expired = time(0L); // insteaf of making long from string again, just get time again as in caller
    writeEntry();

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
void DBEntryV1::writeEntry() {
    if (traceflag)
        spdlog::trace("writeEntry()");
    int perm;
#ifndef WS_RAPIDYAML_DB
    YAML::Node entry;
    entry["workspace"] = workspace;
    entry["creation"] = creation;
    entry["expiration"] = expiration;
    if (expired > 0) {
        entry["expired"] = expired;
    }
    entry["extensions"] = extensions;
    entry["acctcode"] = "";
    entry["reminder"] = reminder;
    entry["mailaddress"] = mailaddress;
    if (groupflag && group.length() > 0) {
        entry["group"] = group;
    }
    if (released > 0) {
        entry["released"] = released;
    }
    entry["comment"] = comment;
#else
    ryml::Tree tree;
    ryml::NodeRef root = tree.rootref();
    root |= ryml::MAP; // mark root as a MAP
    root["workspace"] << workspace;
    root["creation"] << creation;
    root["expiration"] << expiration;
    if (expired > 0) {
        root["expired"] << expired;
    }
    root["extensions"] << extensions;
    root["acctcode"] << "";
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

    // suppress ctrl-c to prevent broken DB entries when FS is hanging and user gets nervous
    signal(SIGINT, SIG_IGN);

    caps.raise_cap({CAP_DAC_OVERRIDE},
                   utils::SrcPos(__FILE__, __LINE__, __func__)); // === Section with raised capabuility START ====

    long dbgid = 0, dbuid = 0;

    if (user::isSetuid()) {
        // for filesystem with root_squash, we need to be DB user here
        dbuid = parent_db->getconfig()->dbuid();
        dbgid = parent_db->getconfig()->dbgid();

        if (debugflag)
            spdlog::debug("isSetuid -> dbuid={}, dbgid={}", dbuid, dbgid);

        if (setegid(dbgid) || seteuid(dbuid)) {
            spdlog::error("can not seteuid or setgid. Bad installation?");
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
    ofstream fout(dbfilepath.c_str());
    if (!(fout << entry)) {
        spdlog::error("could not write DB file! Please check if the outcome is as expected, "
                      "you might have to make a backup of the workspace to prevent loss of data!");
        if (debugflag)
            spdlog::debug("{}", std::strerror(errno));
    }
    fout.close();

    if (group.length() > 0) {
        // for group workspaces, we set the x-bit
        perm = 0744;
    } else {
        perm = 0644;
    }

    caps.raise_cap({CAP_FOWNER}, utils::SrcPos(__FILE__, __LINE__, __func__));
    if (chmod(dbfilepath.c_str(), perm) != 0) {
        spdlog::error("could not change permissions of database entry");
        if (debugflag)
            spdlog::error("{}", std::strerror(errno));
    }

    caps.lower_cap({CAP_FOWNER, CAP_DAC_OVERRIDE}, dbuid, utils::SrcPos(__FILE__, __LINE__, __func__));

    if (caps.isSetuid()) {
        caps.raise_cap({CAP_CHOWN}, utils::SrcPos(__FILE__, __LINE__, __func__));
        if (chown(dbfilepath.c_str(), dbuid, dbgid)) {
            caps.lower_cap({CAP_CHOWN}, dbuid, utils::SrcPos(__FILE__, __LINE__, __func__));
            spdlog::error("could not change owner of database entry.");
        }
        caps.lower_cap({CAP_CHOWN}, dbuid, utils::SrcPos(__FILE__, __LINE__, __func__));
    }

    // normal signal handling
    signal(SIGINT, SIG_DFL);
}

// return config of parent DB
const Config* DBEntryV1::getConfig() const { return parent_db->getconfig(); }
