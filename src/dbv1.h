#ifndef DBV1_H
#define DBV1_H

/*
 *  hpc-workspace-v2
 *
 *  dbv1.h
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

#include "config.h"
#include "db.h"
// #include "caps.h"

class FilesystemDBV1;

// dbentry
//  struct or assoc array?
//  if struct we might need a version field
//  assoc would be very flexible and match the YAML model well, easy to extend
//  and would carry unknown fields simply over. mix may be?
class DBEntryV1 : public DBEntry {
  private:
    // pointer to DB containing this entry (to get access to config)
    FilesystemDBV1* parent_db;

    // information of external format
    int dbversion; // version
    string id;     // ID of this workspace

    // main components of external format
    string filesystem;  // location   // FIXME: is this used anywhere?
    string workspace;   // directory path
    long creation;      // epoch time of creation
    long expiration;    // epoch time of expiration
    long released;      // epoch time of manual release
    long reminder;      // epoch time of reminder to be sent out
    int extensions;     // extensions, counting down
    string group;       // group for whom it is visible
    string mailaddress; // address for reminder email
    string comment;     // some user defined comment
    string dbfilepath;  // if read from DB, this is the location to write to

  public:
    // simple constructor to read from file
    DBEntryV1(FilesystemDBV1* pdb) : parent_db(pdb) {};
    // constructor to make new entry to write out
    DBEntryV1(FilesystemDBV1* pdb, const WsID _id, const string _workspace, const long _creation,
              const long _expiration, const long _reminder, const int _extensions, const string _group,
              const string _mailaddress, const string _comment);

    // read yaml entry from string
    void readFromString(std::string str);
    // read yaml entry from file
    void readFromFile(const WsID id, const string filesystem, const string filename);

    // use extension and write back file
    void useExtension(const long expiration, const string mail, const int reminder, const string comment);
    // change expiration time
    void setExpiration(const time_t timestamp);
    // change release date (mark as released and not expired) and write updated entry and move entry
    void release(const std::string timestamp);
    // write entry to DB after update (read with readEntry) or creation
    void writeEntry();
    // remove entry from DB
    void remove();

    // print for ws_list
    void print(const bool verbose, const bool terse) const;

    long getRemaining() const;
    int getExtension() const;
    string getMailaddress() const;
    string getComment() const;
    string getId() const;
    long getCreation() const;
    string getWSPath() const;
    long getExpiration() const;
    string getFilesystem() const;

    // return config of parent DB
    const Config* getConfig() const;
};

// implementation of V1 DB format from workspace++
class FilesystemDBV1 : public Database {
  private:
    const Config* config;
    string fs;

  public:
    FilesystemDBV1(const Config* config_, const string fs_) : config(config_), fs(fs_) {};

    // create new DB entry
    void createEntry(const WsID id, const string workspace, const long creation, const long expiration,
                     const long reminder, const int extensions, const string group, const string mailaddress,
                     const string comment);

    // read entry
    std::unique_ptr<DBEntry> readEntry(const WsID id, const bool deleted);

    // delete entry
    void deleteEntry(const string wsid, const bool deleted);

    // return list of identifiers of DB entries matching pattern from filesystem or all valid filesystems
    //  does not check if request for "deleted" is valid, has to be done on caller side
    //  throws IO exceptions in case of access problems
    std::vector<WsID> matchPattern(const string pattern, const string user, const vector<string> groups,
                                   const bool deleted, const bool groupworkspaces);

    // create workspace directory according to rules of this DB
    // and return the name
    std::string createWorkspace(const string name, const string user_option, const bool groupflag,
                                const string groupname);

    // access to config
    const Config* getconfig() const { return config; }

    // access to fs
    std::string getfs() { return fs; }
};

#endif
