#ifndef CAPABILITY_H
#define CAPABILITY_H

/*
 *  hpc-workspace-v2
 *
 *  caps.h
 *
 *  - privilege elevation with capabilites or setuid implementation
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

#include <sys/types.h>
#include <unistd.h>

#include <vector>

#include "utils.h"

#ifdef WS_CAPA
    #include <sys/capability.h>
#else // dummy values
typedef int cap_value_t;
const int CAP_CHOWN = 0;
const int CAP_DAC_OVERRIDE = 1;
const int CAP_DAC_READ_SEARCH = 2;
const int CAP_FOWNER = 3;
const int CAP_SYS_ADMIN = 4;
#endif

class Cap {
  public:
    // which supplementary groups to set when changing identity (SUID mode)
    enum class GroupsMode {
        Keep,    // leave current groups untouched
        Clear,   // no supplementary groups (used for DB identity on root_squash filesystems)
        Restore, // restore the groups of the calling user
    };

  private:
    bool hascaps;
    bool issetuid;
    bool isusermode;

    // identity of the calling user, captured before any change is made (constructor)
    uid_t user_uid;
    gid_t user_gid;
    std::vector<gid_t> user_groups;

    // identity of the DB user, set once after the config is parsed via setDbIdentity()
    uid_t db_uid = 0;
    gid_t db_gid = 0;
    bool db_identity_set = false;

    // change euid and egid together; gid and group changes are only done while the
    // process is root (seteuid(0) always works in SUID mode: the saved uid stays 0
    // for the whole run by design, which is what makes raise_cap possible)
    void set_identity(uid_t euid, gid_t egid, GroupsMode groups, utils::SrcPos srcpos);

  public:
    Cap();

    // functions that can be called to raise and lower caps
    void drop_caps(std::vector<cap_value_t> cap_arg, uid_t uid, utils::SrcPos srcpos);
    void lower_cap(std::vector<cap_value_t> cap_arg, uid_t dbuid, utils::SrcPos);
    void raise_cap(std::vector<cap_value_t> cap_arg, utils::SrcPos);

    // tell the Cap object which uid/gid own the database (call once after parsing the
    // config); lower_cap()/become_db() use this to switch uid and gid as a pair
    void setDbIdentity(uid_t dbuid, gid_t dbgid);

    // explicit paired identity transitions (no-ops outside SUID mode)
    void become_root(utils::SrcPos srcpos);
    void become_db(utils::SrcPos srcpos);
    void become_caller(utils::SrcPos srcpos);

    bool isSetuid() { return issetuid; };
    bool hasCaps() { return hascaps; };
    bool isUserMode() { return isusermode; };

    void dump() const;
};

#endif
