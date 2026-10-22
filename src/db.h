#ifndef DB_H
#define DB_H

/*
 *  hpc-workspace-v2
 *
 *  db.h
 *
 *  - interface to database
 *    as main difference to older versions, db is isolated from the tools,
 *    to allow easyer changes
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

#include <exception>
#include <memory>
#include <set>
#include <string>
#include <vector>

using namespace std;

using WsID = std::string;

class Config;

// A databse entry, with methods to
//  - read (called vom DB) ?? FIXME: make friend?
//  - write
//  - consume an extension
//	- print an entry FIXME: move to ws_list?
//  - getters for all data
class DBEntry {
  public:
    // read from DB file // FIXME: this implies a file
    virtual void readFromFile(const WsID id, const string filesystem, const string filename) = 0;

    // write entry to DB after update (read with readEntry)
    virtual void writeEntry(const bool calledasroot = false) = 0;

    // remove db entry
    virtual void remove() = 0;

    // change expiration time
    virtual void setExpiration(const time_t timestamp) = 0;
    // change expired time
    virtual void setExpired(const time_t timestamp) = 0;
    // for an entry from the deleted area (ID "<base>-<ts>"), return the ID "<base>-<ts'>" with the
    // smallest ts' >= boundary that is free: neither a DB entry nor a workspace directory of that
    // name exists in the deleted area, and it is not in reserved (names planned by the caller but
    // not yet created, e.g. in a dry-run). Returns the current ID if that is reached first.
    // Throws DatabaseException for IDs not of that shape or if no free name is found.
    virtual WsID expiredIdFor(const time_t boundary, const std::set<WsID>& reserved) const = 0;
    // rename DB entry and workspace directory of an entry from the deleted area to new_id
    // (as returned by expiredIdFor). DB entry first, workspace second, like ws_expirer.
    // Never overwrites an existing target, rolls back the DB rename if the workspace directory
    // can not be moved. Throws DatabaseException on failure, for user-released entries
    // (their name carries the release time) and for IDs not of the shape "<base>-<ts>".
    virtual void renameAsExpired(const WsID& new_id) = 0;
    // change release date (mark as released and not expired) and write updated entry and move entry
    virtual void release(time_t& timestamp) = 0;
    // set expired (not released) can be called by root only
    virtual void expire(const std::string timestamp) = 0;

    // consume an extension (writes entry back)
    virtual void useExtension(const long expiration, const string mail, const int reminder, const string comment) = 0;

    // getters
    virtual long getRemaining() const = 0;
    virtual string getId() const = 0;
    // owner of this workspace. Entries created by current tools store the username explicitly;
    // for legacy entries without that field, falls back to the dash heuristic (utils::getOwner).
    virtual string getUsername() const = 0;
    virtual int getExtension() const = 0;
    virtual long getCreation() const = 0;
    virtual string getWSPath() const = 0;
    virtual string getMailaddress() const = 0;
    virtual string getComment() const = 0;
    virtual long getExpiration() const = 0;
    virtual long getExpired() const = 0;
    virtual long getReleaseTime() const = 0;
    virtual string getFilesystem() const = 0;
    virtual long getReminder() const = 0;
    virtual string getGroup() const = 0;
    virtual void setGroup(const string& group) = 0;

    // get config of parent DB
    virtual const Config* getConfig() const = 0;

    virtual ~DBEntry() = default; // address-sanitizer needs this
};

// database class, with methods to
//	- create an entry
//	- read an entry
//	- find entries matching a pattern
//  - get a path of a new workspace for that DB
// this class is constructed through Config::openDB
class Database {
  public:
    // new DB entry, username is the exact owner (stored in the `user` field of the entry)
    virtual void createEntry(const string id, const string username, const string workspace, const long creation,
                             const long expiration, const long reminder, const int extensions, const bool groupflag,
                             const string group, const string mailaddress, const string comment) = 0;

    // read specific entry
    virtual std::unique_ptr<DBEntry> readEntry(const WsID id, const bool deleted) = 0;

    // delete entry, can be a deleted one, wsID has to contain timestamp in that case
    virtual void deleteEntry(const WsID, const bool deleted) = 0;

    // return a list of entries
    virtual std::vector<WsID> matchPattern(const string pattern, const string user, const vector<string> groups,
                                           const bool deleted, const bool groupworkspaces) = 0;

    // create workspace directory according to the rules of this Db and return the name
    // has to fix all permissions
    virtual std::string createWorkspace(const string name, const string user_option, const bool groupflag,
                                        const bool groupwritable, const string groupname) = 0;

    virtual ~Database() = default; // address-sanitizer needs this
};

// exception for signaling errors
class DatabaseException : public exception {
  private:
    string message;

  public:
    DatabaseException(const char* msg) : message(msg) {}

    DatabaseException(const std::string msg) : message(msg) {}

    const char* what() const throw() { return message.c_str(); }
};

#endif
