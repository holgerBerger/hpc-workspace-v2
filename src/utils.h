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

#include <regex>
#include <string>
#include <vector>

#include "fmt/core.h"

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

// return names of groups of user given
std::vector<std::string> getgroupnames(std::string username);

// read a small file and returnm as string
std::string getFileContents(const char* filename);
inline std::string getFileContents(const std::string filename) { return getFileContents(filename.c_str()); }

// write a (small) string to a file
void writeFile(const std::string filename, const std::string content);

// retrurn list of filesnames mit unix name globbing
std::vector<std::string> dirEntries(const std::string path, const std::string pattern);

// set C local in every thinkable way
void setCLocal();

// validate an email address (approximation)
bool isValidEmail(const std::string& email);

// get first line of a multiline string
std::string getFirstLine(const std::string& multilineString);

// getID returns id part of workspace id <username-id>
std::string getID(const std::string wsid);

// check if person behind tty is human
// bool ruh();

// aRe yoU Human?
// new version, no terminfo
bool new_ruh();

// parse a ACL
auto parseACL(const std::vector<std::string> acl) -> std::map<std::string, std::pair<std::string, std::vector<int>>>;

} // namespace utils

#endif
