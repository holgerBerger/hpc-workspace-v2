/*
 *  hpc-workspace-v2
 *
 *  user.cpp
 *
 *  - some helpers to deal with user information
 *
 *  c++ version of workspace utility
 *  a workspace is a temporary directory created in behalf of a user with a limited lifetime.
 *
 *  (c) Holger Berger 2021,2023,2024,2025
 *  (c) Christoph Niethammer 2024
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

#include <grp.h>
#include <pwd.h>
#include <string>
#include <sys/types.h>
#include <unistd.h>

#include "user.h"

#include "fmt/base.h"
#include "fmt/ranges.h" // IWYU pragma: keep
#include <gsl/pointers>

extern bool traceflag;
extern bool debugflag;

namespace user {

// get current username via real user id and passwd
std::string getUsername() {
    gsl::not_null<struct passwd*> pw = getpwuid(getuid());
    return std::string(pw->pw_name);
}

// get home of current user via real user id and pwassd
// we have this to avoid $HOME
std::string getUserhome() {
    gsl::not_null<struct passwd*> pw = getpwuid(getuid());
    return std::string(pw->pw_dir);
}

// see if we are root
// return true if current user is root, false otherwise
bool isRoot() {
    // uid is uid of caller, not 0 for setuid(root)
    return getuid() == 0;
}

// check if this process is not setuid
bool isnotSetuid() { return getuid() == geteuid(); }

// check if this process is setuid
bool isSetuid() { return getuid() != geteuid(); }

// get name of effective group
std::string getGroupname() {
    std::string primarygroup;
    gsl::not_null<struct group*> grp = getgrgid(getegid());
    primarygroup = std::string(grp->gr_name);
    return primarygroup;
}

// get list of group names of current process
std::vector<std::string> getGrouplist() {
    if (traceflag)
        fmt::print(stderr, "Trace  : getGroupList()\n");
    std::vector<std::string> grplist;

    // find first size and get list
    int size = getgroups(0, nullptr);

    if (size == -1) {
        fmt::print(stderr, "Error  : error in getgroups()!\n");
        return grplist;
    }

    gsl::not_null<gid_t*> gids = new gid_t[size];
    int ret = getgroups(size, gids);

    for (int i = 0; i < ret; i++) {
        gsl::not_null<struct group*> group = getgrgid(gids.get()[i]);
        grplist.push_back(std::string(group->gr_name));
    }

    delete[] gids;

    if (debugflag)
        fmt::print(stderr, "Debug  : groups={}\n", grplist);

    return grplist;
}

} // namespace user
