/*
 *  hpc-workspace-v2
 *
 *  caps.cpp
 *
 *  - privilege elevation with capabilites or setuid implementation
 *
 *  c++ version of workspace utility
 *  a workspace is a temporary directory created in behalf of a user with a limited lifetime.
 *
 *  (c) Holger Berger 2021,2023,2024,2025
 *  (c) Christoph Niethammer 2024,2025
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

#include <cstdlib>
#include <cstring>

#include <grp.h>

#include "fmt/base.h"   // IWYU pragma: keep
#include "fmt/ranges.h" // IWYU pragma: keep

#include "caps.h"
#include "user.h"

#include "spdlog/spdlog.h"

extern bool traceflag;
extern bool debugflag;

// TODO: is exit(1) ok here? better error handling?

// constructor, check if we are setuid or have capabilites (only if linked with libcap)
Cap::Cap() {
    // capture the identity of the calling user before main() can change anything:
    // a setuid binary starts with ruid/rgid/groups = caller, euid = 0
    user_uid = getuid();
    user_gid = getgid();
    int ngroups = static_cast<int>(getgroups(0, nullptr));
    if (ngroups > 0) {
        user_groups.resize(static_cast<size_t>(ngroups));
        int ret = getgroups(ngroups, user_groups.data());
        if (ret < 0) {
            user_groups.clear();
        } else {
            user_groups.resize(static_cast<size_t>(ret));
        }
    } else if (ngroups < 0) {
        ngroups = 0;
    }

    // use those to enable tracing here, is called before arguments are parsed
    // debugflag = true;
    // traceflag = true;
    if (traceflag)
        spdlog::trace("Cap::Cap()");
    if (debugflag)
        spdlog::debug("euid={}, uid={}", geteuid(), getuid());

    issetuid = user::isSetuid();
    hascaps = false;
    isusermode = false;

    if (!issetuid) {
#ifdef WS_CAPA
        cap_t caps, oldcaps;
        cap_value_t cap_list[1];

        caps = cap_get_proc();
        if (NULL == caps) {
            spdlog::error("Failed to obtain capabilities.");
            exit(1);
        }
        oldcaps = cap_dup(caps);
        if (NULL == oldcaps) {
            spdlog::error("Failed copying capabilities.");
            cap_free(caps);
            exit(1);
        }

        cap_list[0] = CAP_DAC_OVERRIDE; // this has to be set for all executables using this!

        if (cap_set_flag(caps, CAP_EFFECTIVE, 1, cap_list, CAP_SET) == -1) {
            spdlog::error("Failed updating effective capability set.");
            cap_free(caps);
            cap_free(oldcaps);
            exit(1);
        }

        if (cap_set_proc(caps) == 0) {
            if (cap_set_proc(oldcaps) != 0) {
                spdlog::error("Failed resetting capabilities.");
                cap_free(caps);
                cap_free(oldcaps);
                exit(1);
            }
            hascaps = true;
        }

        cap_free(caps);
        cap_free(oldcaps);
#endif
    }

    if (!issetuid && !hascaps) {
        isusermode = true;
    }

    if (debugflag) {
#ifdef WS_CAPA
        spdlog::debug("libcap is linked");
#endif
        spdlog::debug("issetuid={} hascaps={} isusermode={}", issetuid, hascaps, isusermode);
    }
}

// change effective uid and gid as one atomic-feeling operation; only active in SUID mode.
// The saved uid is 0 for the whole run in SUID mode (seteuid() does not touch it), so
// seteuid(0) always works and gives us the root moment needed for setegid()/setgroups().
void Cap::set_identity(uid_t euid, gid_t egid, GroupsMode groups, utils::SrcPos srcpos) {
    if (!issetuid)
        return;

    if (debugflag)
        spdlog::debug("Cap::set_identity(-> euid={}, egid={}, groups={}) from {}", euid, egid,
                      groups == GroupsMode::Keep ? "keep" : (groups == GroupsMode::Clear ? "clear" : "restore"),
                      srcpos.getSrcPos());

    if (seteuid(0)) {
        spdlog::error("can not raise to root: {} ({}) {}", errno, strerror(errno), srcpos.getSrcPos());
        exit(1);
    }

    switch (groups) {
    case GroupsMode::Clear:
        if (setgroups(0, nullptr)) {
            spdlog::error("can not drop supplementary groups: {} ({}) {}", errno, strerror(errno), srcpos.getSrcPos());
            exit(1);
        }
        break;
    case GroupsMode::Restore:
        if (setgroups(user_groups.empty() ? 0 : static_cast<size_t>(user_groups.size()), user_groups.data())) {
            spdlog::error("can not restore supplementary groups: {} ({}) {}", errno, strerror(errno),
                          srcpos.getSrcPos());
            exit(1);
        }
        break;
    case GroupsMode::Keep:
        break;
    }

    if (setegid(egid)) {
        spdlog::error("can not change gid to {}: {} ({}) {}", egid, errno, strerror(errno), srcpos.getSrcPos());
        exit(1);
    }
    if (seteuid(euid)) {
        spdlog::error("can not change uid to {}: {} ({}) {}", euid, errno, strerror(errno), srcpos.getSrcPos());
        exit(1);
    }
}

void Cap::setDbIdentity(uid_t dbuid, gid_t dbgid) {
    db_uid = dbuid;
    db_gid = dbgid;
    db_identity_set = true;
}

void Cap::become_root(utils::SrcPos srcpos) {
    // root bypasses DAC checks locally, groups are irrelevant; keep them as they are
    set_identity(0, 0, GroupsMode::Keep, srcpos);
}

void Cap::become_db(utils::SrcPos srcpos) {
    if (!issetuid)
        return;
    if (!db_identity_set) {
        spdlog::error("db identity not set before become_db() {}", srcpos.getSrcPos());
        exit(1);
    }
    // the DB identity must not carry the caller's supplementary groups: on NFS with
    // root_squash we act as the DB user, and user groups could grant unintended access
    set_identity(db_uid, db_gid, GroupsMode::Clear, srcpos);
}

void Cap::become_caller(utils::SrcPos srcpos) { set_identity(user_uid, user_gid, GroupsMode::Restore, srcpos); }

// drop root/effective capabilities (not necessary), and verify the permitted set
void Cap::drop_caps(std::vector<cap_value_t> cap_arg, uid_t uid, utils::SrcPos srcpos) {
    if (traceflag) {
        spdlog::trace("Cap::dropcap( {}, {}, {})", uid, cap_arg, srcpos.getSrcPos());
        if (debugflag)
            dump();
    }
#ifdef WS_CAPA
    if (hascaps) {
        cap_t caps;

        caps = cap_init();

        // setting caps we should have in PERMITTED set
        if (cap_set_flag(caps, CAP_PERMITTED, static_cast<int>(cap_arg.size()), cap_arg.data(), CAP_SET) == -1) {
            spdlog::error("problem with permitted capabilities. {}", srcpos.getSrcPos());
            if (debugflag)
                dump();
            exit(1);
        }

        if (cap_set_proc(caps) != 0) {
            spdlog::error("problem setting permitted capabilities.");
            if (debugflag)
                dump();
            cap_free(caps);
            exit(1);
        }

        cap_free(caps);
    }
#endif

    if (issetuid) {
        // become the calling user again: euid, egid and supplementary groups as a pair
        set_identity(uid, user_gid, GroupsMode::Restore, srcpos);
    }
}

// remove a capability from the effective set
void Cap::lower_cap(std::vector<cap_value_t> cap_arg, uid_t uid, utils::SrcPos srcpos) {
    if (traceflag) {
        spdlog::trace("Cap::lower_cap( {}, {})", uid, srcpos.getSrcPos());
        dump();
    }
#ifdef WS_CAPA
    if (hascaps) {
        cap_t caps;

        caps = cap_get_proc();

        if (cap_set_flag(caps, CAP_EFFECTIVE, static_cast<int>(cap_arg.size()), cap_arg.data(), CAP_CLEAR) == -1) {
            spdlog::error("problem with effective capabilities {}.", srcpos.getSrcPos());
            exit(1);
        }

        if (cap_set_proc(caps) == -1) {
            spdlog::error("problem lowering effective capabilities {}.", srcpos.getSrcPos());
            cap_t cap = cap_get_proc();
            char *cap_text = cap_to_text(cap, NULL);
            spdlog::debug("Running with capabilities: {}", cap_text);
            cap_free(cap_text);
            cap_free(cap);
            exit(1);
        }

        cap_free(caps);
    }
#endif

    if (issetuid) {
        if (db_identity_set) {
            // lowered state is the DB identity (paired uid+gid, no caller groups)
            set_identity(db_uid, db_gid, GroupsMode::Clear, srcpos);
        } else {
            // legacy fallback: no gid known, behave like before
            if (seteuid(uid)) {
                spdlog::error("can not change uid {}.", srcpos.getSrcPos());
                exit(1);
            }
        }
    }
}

// add a capability to the effective set
void Cap::raise_cap(std::vector<cap_value_t> cap_arg, utils::SrcPos srcpos) {
    if (traceflag) {
        spdlog::trace("Cap::raise_cap( {}, {})", cap_arg, srcpos.getSrcPos());
        dump();
    }
#ifdef WS_CAPA
    if (hascaps) {
        cap_t caps;

        caps = cap_get_proc();

        if (cap_set_flag(caps, CAP_EFFECTIVE, static_cast<int>(cap_arg.size()), cap_arg.data(), CAP_SET) == -1) {
            spdlog::error("problem with effective capabilities {}.", srcpos.getSrcPos());
            exit(1);
        }

        if (cap_set_proc(caps) == -1) {
            spdlog::error("problem raising effective capabilities. {}", srcpos.getSrcPos());
            cap_t cap = cap_get_proc();
            char *cap_text = cap_to_text(cap, NULL);
            spdlog::debug("Running with capabilities: {}", cap_text);
            cap_free(cap_text);
            cap_free(cap);
            exit(1);
        }

        cap_free(caps);
    }
#endif

    if (issetuid) {
        // root identity: euid and egid as a pair
        become_root(srcpos);
    }
}

// helper to dump current capabilities
void Cap::dump() const {
#ifdef WS_CAPA
    cap_t cap = cap_get_proc();
    char* cap_text = cap_to_text(cap, NULL);
    spdlog::debug("running with capabilities: {}", cap_text);
    cap_free(cap_text);
    cap_free(cap);
#endif
}
