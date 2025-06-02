#ifndef USER_H
#define USER_H

/*
 *  hpc-workspace-v2
 *
 *  user.h
 *
 *  - helpers to deal with user information
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

#include <pwd.h>
#include <sys/types.h>

namespace user {
std::string getUsername();
std::string getUserhome();
bool isRoot();
bool isnotSetuid();
bool isSetuid();
std::string getGroupname();
std::vector<std::string> getGrouplist();
} // namespace user

#endif
