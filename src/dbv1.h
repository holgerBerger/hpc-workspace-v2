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

#include <vector>
#include <string>

#include "config.h"
#include "db.h"
#include "caps.h"

class FilesystemDBV1;

// dbentry
//  struct or assoc array?
//  if struct we might need a version field
//  assoc would be very flexible and match the YAML model well, easy to extend
//  and would carry unknown fields simply over. mix may be?
class DBEntryV1 : public DBEntry {
private:
        // information of external format
        int     dbversion;              // version
        string  id;                     // ID of this workspace

        // main components of external format
        string  filesystem;             // location
        string  workspace;              // directory path
        long    creation;               // epoch time of creation
        long    expiration;             // epoch time of expiration
        long    released;               // epoch time of manual release
        long    reminder;               // epoch time of reminder to be sent out
        int     extensions;             // extensions, counting down
        string  group;                  // group for whom it is visible
        string  mailaddress;            // address for reminder email
        string  comment;                // some user defined comment
        string  dbfilepath;             // if read from DB, this is the location to write to

        // pointer to DB containing this entry (to get access to config)
        FilesystemDBV1 *db;


public:

        void readFromString(std::string str);
        // read yaml entry from file
        void readFromFile(const WsID id, const string filesystem, const string filename);
        // use extension and write back file
        void useExtension(const long expiration, const string mail, const int reminder, const string comment);
	// write entry to DB after update (read with readEntry)
	void writeEntry();
        // print for ws_list
        void print(const bool verbose, const bool terse) const;
        long getRemaining() const;
        int getExtension() const;
        string getMailaddress() const;
        string getId() const;
        long getCreation() const;
        string getWSPath() const;
        long getExpiration() const;


};



// implementation of V1 DB format from workspace++
class FilesystemDBV1 : public Database {
private:
        const Config* config;
        string fs;
public:
        FilesystemDBV1(const Config* config_, const string fs_) : config(config_), fs(fs_) {};

        // return list of identifiers of DB entries matching pattern from filesystem or all valid filesystems
        //  does not check if request for "deleted" is valid, has to be done on caller side
        //  throws IO exceptions in case of access problems
        std::vector<WsID> matchPattern(const string pattern, const string user, const vector<string> groups,
                                                const bool deleted, const bool groupworkspaces);

        // create new DB entry
        void createEntry(const string user, const string id, const string workspace, 
			const long creation, const long expiration, const long reminder, const int extensions, 
			const string group, const string mailaddress, const string comment) {} // TODO: };

	// read entry
	DBEntry* readEntry(const WsID id, const bool deleted);

        // access to config
        const Config *getconfig() {
                return config;
        }
};

#endif
