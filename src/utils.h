#ifndef UTILS_H
#define UTILS_H

/*
 *  hpc-workspace-v2
 *
 *  utils.h
 *
 *  - helper functions
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
#include <ctime>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "fmt/core.h"
#include "fmt/format.h"
#include "user.h"

namespace utils {

// helper to show sourcelocation in debugging
class SrcPos {
    std::string file;
    int line;
    std::string func;

  public:
    SrcPos(const char* _file, const int _line, const char* _func) : file(_file), line(_line), func(_func) {};
    std::string getSrcPos() { return fmt::format("{}:{}[{}]", file, line, func); }
};

// read a small file and returnm as string
std::string getFileContents(const char* filename);
inline std::string getFileContents(const std::string filename) { return getFileContents(filename.c_str()); }

// write a (small) string to a file
void writeFile(const std::string filename, const std::string content);

// retrurn list of filesnames mit unix name globbing
std::vector<std::string> dirEntries(const std::string path, const std::string pattern, const bool dirs);

// set C local in every thinkable way
void setCLocal();

// validate an email address (approximation)
bool isValidEmail(const std::string& email);

// get first line of a multiline string
std::string getFirstLine(const std::string& multilineString);

// getID returns id part of workspace id <username-id>
std::string getID(const std::string username, const std::string wsid);

// check if person behind tty is human
// bool ruh();

// aRe yoU Human?
// new version, no terminfo
bool new_ruh();

// parse a ACL
auto parseACL(const std::vector<std::string> acl) -> std::map<std::string, std::pair<std::string, std::vector<int>>>;

// delete a directory and its contents, should be temper safe, with a deadline (epoch time)
// after the deadline is passed, no new recursion will be started,
// so the deadline is not hard but soft and can be missed significantly
// deadline==0 disables the deadline
void rmtree(std::string path, const std::time_t deadline);

// delete a directory and its contents, should be temper safe
void rmtree(std::string path);

// delete a directorys contents, but not the directory itself
void rmtree_below(std::string path) ;

// split a string at delimiter and return vector
std::vector<std::string> splitString(const std::string& str, char delimiter);

// pretty print a size into a string with KB/GB etc unit
std::string prettyBytes(const uint64_t size);

// setup the logging
void setupLogging(const std::string ident);

// right trim whitespaces from a string
std::string trimright(const std::string& in);
std::string trimright(const char* in);

// class to check existance of intersection of group lists of two users, on user provided through constructor,
// other user provided through method, with caching for result
// returns true if second user is unknown (empty group list)
class HasGroupIntersection {
  private:
    std::vector<std::string> baselist;
    std::map<std::string, bool> resultcache;

  public:
    // call this with caller user
    HasGroupIntersection(const std::string baseuser) {
        baselist = user::getUserGroupList(baseuser);
        std::sort(baselist.begin(), baselist.end());
    };
    // call this with another user to check if groups ov the two overlap
    bool hasCommonGroups(const std::string user) {
        auto it = resultcache.find(user);
        if (it != resultcache.end())
            return it->second;

        // not found
        auto comparelist = user::getUserGroupList(user);
        // if this is empty, user is probably unknown, we better assume there is some intersection
        if (comparelist.size() == 0) {
            resultcache[user] = true;
            return true;
        }
        std::sort(comparelist.begin(), comparelist.end());

        std::vector<std::string> intersection;
        std::set_intersection(baselist.begin(), baselist.end(), comparelist.begin(), comparelist.end(),
                              std::back_inserter(intersection));

        if (intersection.size() > 0) {
            resultcache[user] = true;
            return true;
        } else {
            resultcache[user] = false;
            return false;
        }
    };
};

// thread safe ctime implementation, use this where std::ctime was used
// please note: this does NOT append a \n!
std::string ctime(const time_t* timer);
std::string ctime(const time_t timer);

// get a string like rwx------ for permissions
std::string permstring(std::filesystem::perms p);

// move a file/directory to another location using /bin/mv, fallback for rename EXDEV
int mv(const char* source, const char* target);

} // namespace utils

#endif
