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


#include <string>
#include <vector>
#include <filesystem>

// use for speed, but needs testing // FIXME:
#define WS_RAPIDYAML_DB

#ifdef WS_RAPIDYAML_DB
    #define RYML_USE_ASSERT 0 
    #include "ryml.hpp"
    #include "ryml_std.hpp" 
    #include "c4/format.hpp" 
#else
    #include <yaml-cpp/yaml.h>
#endif

#include "dbv1.h"
#include "utils.h"
#include "user.h"
#include "fmt/base.h"
#include "fmt/ranges.h"

#include <signal.h>
// for chown
#include <sys/stat.h>
#include <fcntl.h>

#include <fstream>
#include <iostream>

#include "caps.h"

using namespace std;

extern bool debugflag;
extern bool traceflag;
extern Cap caps;

namespace cppfs = std::filesystem;


vector<WsID> FilesystemDBV1::matchPattern(const string pattern, const string user, const vector<string> groups,
                                                const bool deleted, const bool groupworkspaces)
{
    if(traceflag) fmt::print(stderr, "Trace  : matchPattern(pattern={},user={},groups={},deleted={},groupworkspace={})\n",pattern, user, groups, deleted,groupworkspaces);

    string filepattern;

    // list directory, this also reads YAML file in case of groupworkspaces
    auto listdir = [&groupworkspaces, &groups] (const string pathname, const string filepattern) -> vector<string> {
        if(debugflag) fmt::print("listdir({},{})\n", pathname, filepattern);
        // in case of groupworkspace, read entry
        if (groupworkspaces) {
            auto filelist = utils::dirEntries(pathname, filepattern);
            vector<string> list;
            for(auto const &f: filelist) {  
#ifndef WS_RAPIDYAML_DB
                YAML::Node dbentry;
                try {
                    dbentry = YAML::LoadFile((cppfs::path(pathname) / f).string().c_str());
                } catch (const YAML::BadFile& e) {
                    fmt::print(stderr,"Error  : Could not read db entry {}: {}", f, e.what());
                }

                string group = dbentry["group"].as<string>();
#else
                string filecontent = utils::getFileContents((cppfs::path(pathname) / f).string().c_str());
                ryml::Tree dbentry = ryml::parse_in_place(ryml::to_substr(filecontent));                // FIXME: error check?

                ryml::NodeRef node;
                string group;
                node=dbentry["group"]; if(node.has_val()&& node.val()!="") node>>group; else group = "";
#endif

                if (canFind(groups, group)) {
                        list.push_back(f);
                }
            }         
            return list;   
        } else {
            return utils::dirEntries(pathname, filepattern);
        }
    };

    // this has to happen here, as other DB might have different patterns
    if (groupworkspaces)
            filepattern = fmt::format("*-{}",pattern);
    else
            filepattern = fmt::format("{}-{}", user,pattern);

    // scan filesystem
    if (deleted)
            return listdir(cppfs::path(config->database(fs)) / config->deletedPath(fs), filepattern);
    else
            return listdir(config->database(fs), filepattern);
}


// read entry
// unittest: TODO: error handling
DBEntry* FilesystemDBV1::readEntry(const WsID id, const bool deleted) {
    if(traceflag) fmt::print(stderr, "Trace  : readEntry({},{})\n", id, deleted);
    auto *entry = new DBEntryV1;
    string filename;
    if (deleted) 
        filename = cppfs::path(config->database(fs)) / config->deletedPath(fs) / id;
    else
        filename = cppfs::path(config->database(fs)) / id;
    entry->readFromFile(id, fs, filename);

    return entry;
}


// read db entry from yaml file
void DBEntryV1::readFromFile(const WsID id, const string filesystem, const string filename) {
    if(traceflag) fmt::print(stderr, "Trace  : readFromFile({},{},{})\n", id, filesystem, filename);

    this->id = id;
    this->filesystem = filesystem;
    this->dbfilepath = filename; // store location if db entry for later writing


    std::string filecontent = utils::getFileContents(filename.c_str());
    if(filecontent == "") {
        fmt::print(stderr,"Error  : Could not read file {}", filename);
        throw DatabaseException("could not read file");
    }

    readFromString(filecontent);

    if(debugflag) {
        fmt::print(stderr, "Debug  : creation={} released={} expiration={} reminder={} workspace={} extensions={} mailaddress={} comment={} group={}\n" , 
                    creation, released, expiration, reminder, workspace, extensions, mailaddress, comment, group);
    }
}

#ifndef WS_RAPIDYAML_DB
// use yamlcpp

void ::DBEntry::readFromString(std::string str) {
    if(traceflag) fmt::print(stderr, "Trace  : readFromString_YAMLCPP\n");

    YAML::Node dbentry;
    try {
        dbentry = YAML::Load(str);
    } catch (const YAML::BadFile& e) {
        fmt::print(stderr,"Error  : Could not read db entry {}: {}", filename, e.what());
        throw DatabaseException("could not read db entry");
    }

    dbversion = dbentry["dbversion"] ? dbentry["dbversion"].as<int>() : 0;   // 0 = legacy
    creation = dbentry["creation"] ? dbentry["creation"].as<long>() : 0;  // FIXME: c++ tool does not write this field, but takes from stat
    released = dbentry["released"] ? dbentry["released"].as<long>() : 0;
    expiration = dbentry["expiration"] ? dbentry["expiration"].as<long>() : 0;
    reminder = dbentry["reminder"] ? dbentry["reminder"].as<long>() : 0;
    workspace = dbentry["workspace"] ? dbentry["workspace"].as<string>() : "";
    extensions = dbentry["extensions"] ? dbentry["extensions"].as<int>() : 0;
    mailaddress = dbentry["mailaddress"] ? dbentry["mailaddress"].as<string>() : "";
    comment = dbentry["comment"] ? dbentry["comment"].as<string>() : "";
    group = dbentry["group"] ? dbentry["group"].as<string>() : ""; 
}

#else
// use rapidyaml

void DBEntryV1::readFromString(std::string str) {
    if(traceflag) fmt::print(stderr, "Trace  : readFromString_RAPIDYAML\n");

    ryml::Tree dbentry = ryml::parse_in_place(ryml::to_substr(str));  // FIXME: error check?

    ryml::NodeRef node;

    node=dbentry["dbversion"]; if(node.has_val()) node>>dbversion; else dbversion = 0;  // 0 = legacy
    node=dbentry["creation"]; if(node.has_val()) node>>creation; else creation = 0;  // FIXME: c++ tool does not write this field, but takes from stat
    node=dbentry["released"]; if(node.has_val()) node>>released; else released = 0;
    node=dbentry["expiration"]; if(node.has_val()) node>>expiration; else expiration = 0;
    node=dbentry["reminder"]; if(node.has_val()) node>>reminder; else reminder = 0;
    node=dbentry["workspace"]; if(node.has_val() && node.val()!="") node>>workspace; else workspace = "";
    node=dbentry["extensions"]; if(node.has_val()) node>>extensions; else extensions = 0;
    node=dbentry["mailaddress"]; if(node.has_val() && node.val()!="") node>>mailaddress; else mailaddress = "";
    node=dbentry["comment"]; if(node.has_val() && node.val()!="") node>>comment; else comment = "";
    node=dbentry["group"]; if(node.has_val() && node.val()!="") node>>group; else group = "";
}

#endif



// print entry to stdout, for ws_list, should be only called lock protected, is not thread safe due to std::ctime
void DBEntryV1::print(const bool verbose, const bool terse) const {
    string repr;
    long remaining = expiration - time(0L);

    fmt::print(
        "Id: {}\n"
        "    workspace directory  : {}\n",
        id, workspace);
    if (remaining<0) {
        fmt::print("    remaining time       : {}\n", "expired");
    } else {
        fmt::print("    remaining time       : {} days, {} hours\n", remaining/(24*3600), (remaining%(24*3600))/3600);
    }
    if(!terse) {
        if(comment!="")
            fmt::print("    comment              : {}\n", comment);
        if (creation>0)
            fmt::print("    creation time        : {}", ctime(&creation));
        fmt::print("    expiration time      : {}", ctime(&expiration));
        fmt::print("    filesystem name      : {}\n", filesystem);
    }
    fmt::print("    available extensions : {}\n", extensions);
    if (verbose) {
        long rd = expiration - reminder/(24*3600);
        fmt::print("    reminder             : {}", ctime(&rd));
        fmt::print("    mailaddress          : {}\n", mailaddress);
    }
};


// Use extension or update content of entry
void DBEntryV1::useExtension(const long _expiration, const string _mailaddress, const int _reminder, const string _comment) {
    if(traceflag) fmt::print(stderr, "Trace  : useExtension(expiration={},mailaddress={},reminder={},comment={})\n");
    if (_mailaddress!="") mailaddress=_mailaddress;
    if (_reminder!=0) reminder=_reminder;
    if (_comment!="") comment=_comment;
    // if root does this, we do not use an extension
    if((getuid()!=0) && (_expiration!=-1) && (_expiration > expiration)) extensions--;
    if((extensions<0) && (getuid()!=0)) {
        fmt::print(stderr, "Error  : no more extensions.\n");
        exit(-1);  // FIXME: throw!!
    }
    if (_expiration!=-1) {
        expiration = _expiration;
    }
    writeEntry();
}


long DBEntryV1::getRemaining() const {
    return expiration - time(0L);
};

int DBEntryV1::getExtension() const {
    //if(traceflag) fmt::print(stderr, "Trace  : getExtension()\n");
    //if(debugflag) fmt::print(stderr, "Debug  : extensions={}\n", extensions);
    return extensions;
}

string DBEntryV1::getId() const {
    return id;
}

long DBEntryV1::getCreation() const {
    return creation;
}

string DBEntryV1::getWSPath() const {
    return workspace;
};
		
string DBEntryV1::getMailaddress() const {
    return mailaddress;
}

long DBEntryV1::getExpiration() const {
    return expiration;
}


// write data to file
void DBEntryV1::writeEntry()
{
    if(traceflag) fmt::print(stderr, "Trace  : writeEntry()\n");
    int perm;
#ifndef WS_RAPIDYAML_DB
    YAML::Node entry;
    entry["workspace"] = workspace;
    entry["expiration"] = expiration;
    entry["extensions"] = extensions;
    // entry["acctcode"] = acctcode;  // FIXME: ???
    entry["reminder"] = reminder;
    entry["mailaddress"] = mailaddress;
    if (group.length()>0) {
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
    root["expiration"] << expiration;
    root["extensions"] << extensions;
    // root["acctcode"] << acctcode;  FIXME: ???
    root["reminder"] << reminder;
    root["mailaddress"] << mailaddress;
    if (group.length()>0) {
        root["group"] << group;
    }
    if (released > 0) {
        root["released"] << released;
    }
    root["comment"] << comment;

    std::string entry = ryml::emitrs_yaml<std::string>(tree);
#endif

    // suppress ctrl-c to prevent broken DB entries when FS is hanging and user gets nervous
    signal(SIGINT,SIG_IGN);

    caps.raise_cap(CAP_DAC_OVERRIDE, utils::SrcPos(__FILE__, __LINE__, __func__));   // === Section with raised capabuility START ====

    long dbgid=0, dbuid=0;    

    if (user::isSetuid()) {
        // for filesystem with root_squash, we need to be DB user here
        dbgid = db->getconfig()->dbgid();
        dbuid = db->getconfig()->dbuid();

        if (setegid(dbgid)|| seteuid(dbuid)) {
                // FIXME: usermode
                fmt::print(stderr, "Error  : can not seteuid or setgid. Bad installation?\n");
                exit(-1);
        }
    }

    ofstream fout(dbfilepath.c_str());
    if(!(fout << entry)) {
        fmt::print(stderr, "Error  : could not write DB file! Please check if the outcome is as expected, "
                            "you might have to make a backup of the workspace to prevent loss of data!\n");
    }
    fout.close();

    if (group.length()>0) {
        // for group workspaces, we set the x-bit
        perm = 0744;
    } else {
        perm = 0644;
    }

    caps.raise_cap(CAP_FOWNER, utils::SrcPos(__FILE__, __LINE__, __func__));
    if (chmod(dbfilepath.c_str(), perm) != 0) {
        fmt::print(stderr, "Error  : could not change permissions of database entry\n");
    }

    caps.lower_cap(CAP_FOWNER, dbuid, utils::SrcPos(__FILE__, __LINE__, __func__));
    caps.lower_cap(CAP_DAC_OVERRIDE, dbuid, utils::SrcPos(__FILE__, __LINE__, __func__)); // === Section with raised capabuility END ===


    if (caps.isSetuid()) {
        caps.raise_cap(CAP_CHOWN, utils::SrcPos(__FILE__, __LINE__, __func__));
        if (chown(dbfilepath.c_str(), dbuid, dbgid)) {
            caps.lower_cap(CAP_CHOWN, dbuid, utils::SrcPos(__FILE__, __LINE__, __func__));
            fmt::print(stderr, "Error  : could not change owner of database entry.\n");
        }
        caps.lower_cap(CAP_CHOWN, dbuid, utils::SrcPos(__FILE__, __LINE__, __func__));
    }

    // normal signal handling
    signal(SIGINT,SIG_DFL);
}
