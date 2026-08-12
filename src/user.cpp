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
#include <set>
#include <string>
#include <sys/types.h>
#include <unistd.h>

#include "user.h"

#include "fmt/base.h"   // IWYU pragma: keep
#include "fmt/ranges.h" // IWYU pragma: keep
#include <gsl/pointers>

#include "spdlog/spdlog.h"

extern bool traceflag;
extern bool debugflag;
extern int debuglevel;

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
        spdlog::trace("getGroupList()");
    std::vector<std::string> grplist;

    // find first size and get list
    int size = getgroups(0, nullptr);

    if (size == -1) {
        spdlog::error("error in getgroups()!");
        return grplist;
    }

    gsl::not_null<gid_t*> gids = new gid_t[size];
    int ret = getgroups(size, gids);

    for (int i = 0; i < ret; i++) {
        struct group* group = getgrgid(gids.get()[i]);
        // this pointer can get null, happens e.g. with PBSPro which assigns a group to a process
        // that does not exist
        if (group == nullptr) {
            continue;
        }
        grplist.push_back(std::string(group->gr_name));
    }

    delete[] gids;

    if (debugflag && debuglevel > 0)
        spdlog::debug("groups={}", grplist);

    return grplist;
}

// return a list of groupnames for a given user
std::vector<std::string> getUserGroupList(const std::string& username) {
    std::vector<std::string> groupList;
    struct passwd* pw;

    // Get user information by username
    pw = getpwnam(username.c_str());
    if (pw == nullptr) {
        if (debugflag && debuglevel > 0)
            spdlog::debug("user {} not found.", username);
        return groupList; // Return empty vector
    }

    // Get primary group name
    struct group* gr = getgrgid(pw->pw_gid);
    if (gr != nullptr) {
        groupList.push_back(gr->gr_name);
    } else {
        spdlog::warn("could not get primary group name for GID {}", pw->pw_gid);
    }

    // Get supplementary groups
    // getgrouplist needs a buffer for gids. We'll start with a reasonable size
    // and resize if needed.
    int ngroups = 10; // Initial guess for number of groups
    std::vector<gid_t> gids(ngroups);

    int result = getgrouplist(username.c_str(), pw->pw_gid, gids.data(), &ngroups);

    if (result == -1) {
        // Buffer was too small. ngroups now contains the required size.
        gids.resize(ngroups);
        result = getgrouplist(username.c_str(), pw->pw_gid, gids.data(), &ngroups);
    }

    if (result == -1) {
        spdlog::warn("getgrouplist failed for user ", username);
        return groupList; // Return what we have so far (primary group, if any)
    }

    // Use a set to store unique group names, as getgrouplist might return
    // the primary group name again.
    std::set<std::string> uniqueGroupNames;
    if (!groupList.empty()) { // Add primary group if it was successfully found
        uniqueGroupNames.insert(groupList[0]);
    }

    for (int i = 0; i < ngroups; ++i) {
        gr = getgrgid(gids[i]);
        if (gr != nullptr) {
            uniqueGroupNames.insert(gr->gr_name);
        } else {
            spdlog::warn("could not get group name for GID {}", gids[i]);
        }
    }

    // Convert set back to vector
    groupList.assign(uniqueGroupNames.begin(), uniqueGroupNames.end());

    return groupList;
}

} // namespace user
