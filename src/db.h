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

#include <string>
#include <vector>
#include <exception>
#include <memory>

using namespace std;

using WsID = std::string;

/*
struct WsID {
	string user;
	string id;
};
*/


class DBEntry {
public:
		// read from DB file // FIXME: this implies a file
		virtual void readFromFile(const WsID id, const string filesystem, const string filename) = 0;
		// consume an extension (writes entry back)
		virtual void useExtension(const long expiration, const string mail, const int reminder, const string comment) = 0;
		// write entry to DB after update (read with readEntry)
		void writeEntry();
		// print for ws_list
        virtual void print(const bool verbose, const bool terse) const = 0;

		// getters
        virtual long getRemaining() const = 0;
        virtual string getId() const = 0;
        virtual int getExtension() const =0;
		virtual long getCreation() const = 0;
        virtual string getWSPath() const = 0;
		virtual string getMailaddress() const = 0;
		virtual long getExpiration() const = 0;

		virtual ~DBEntry() = default;  // address-sanitizer needs this
};


class Database {
public:
	// new entry
	virtual void createEntry(const string user, const string id, const string workspace, 
			const long creation, const long expiration, const long reminder, const int extensions, 
			const string group, const string mailaddress, const string comment) = 0;
		// FIXME: acctcode? where is it?
	
	// read specific entry
	virtual std::unique_ptr<DBEntry> readEntry(const WsID id, const bool deleted) = 0;
	
	// return a list of entries
	virtual std::vector<WsID> matchPattern(const string pattern, const string user, 
			const vector<string> groups, const bool deleted, const bool groupworkspaces) = 0;

	virtual ~Database() = default; // address-sanitizer needs this
};


// exception for signalling errors
class DatabaseException : public exception {
private:
    string message;

public:
    DatabaseException(const char* msg)
        : message(msg)
    {
    }

    const char* what() const throw()
    {
        return message.c_str();
    }
};


#endif
