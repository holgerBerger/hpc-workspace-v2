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
#include <sys/capability.h>

#include "fmt/base.h"
#include "fmt/ranges.h" // IWYU pragma: keep

#include "caps.h"
#include "user.h"

extern bool traceflag;
extern bool debugflag;

// TODO: is exit(1) ok here? better error handling?

// constructor, check if we are setuid or have capabilites (only if linked with libcap)
Cap::Cap() {
    // use those to enable tracing here, is called before arguments are parsed
    // debugflag = true;
    // traceflag = true;
    if (traceflag)
        fmt::println(stderr, "Trace  : Cap::Cap()");
    if (debugflag)
        fmt::println(stderr, "Debug  : euid={}, uid={}", geteuid(), getuid());

    issetuid = user::isSetuid();
    hascaps = false;
    isusermode = false;

    if (!issetuid) {
#ifdef WS_CAPA
        cap_t caps, oldcaps;
        cap_value_t cap_list[1];

        caps = cap_get_proc();
        if (NULL == caps) {
            fmt::println(stderr, "Error: Failed to obtain capabilities.");
            exit(1);
        }
        oldcaps = cap_dup(caps);
        if (NULL == oldcaps) {
            fmt::println(stderr, "Error: Failed copying capabilities.");
            cap_free(caps);
            exit(1);
        }

        cap_list[0] = CAP_DAC_OVERRIDE; // this has to be set for all executables using this!

        if (cap_set_flag(caps, CAP_EFFECTIVE, 1, cap_list, CAP_SET) == -1) {
            fmt::println(stderr, "Error: Failed updating effective capability set.");
            cap_free(caps);
            cap_free(oldcaps);
            exit(1);
        }

        if (cap_set_proc(caps) == 0) {
            if (cap_set_proc(oldcaps) != 0) {
                fmt::println("Error: Failed resetting capabilities.");
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
        fmt::println(stderr, "Debug  : libcap is linked");
#endif
        fmt::println(stderr, "Debug  : issetuid={} hascaps={} isusermode={}", issetuid, hascaps, isusermode);
    }
}

// drop root/effective capabilities (not necessary), and verify the permitted set
void Cap::drop_caps(std::vector<cap_value_t> cap_arg, int uid, utils::SrcPos srcpos) {
    if (traceflag) {
        fmt::println(stderr, "Trace  : Cap::dropcap( {}, {}, {})", uid, cap_arg, srcpos.getSrcPos());
        if (debugflag)
            dump();
    }
#ifdef WS_CAPA
    if (hascaps) {
        cap_t caps;
        cap_value_t cap_list[cap_arg.size()]; // FIXME: VLA not in ISO C++

        int cnt = 0;
        for (const auto& ca : cap_arg) {
            cap_list[cnt++] = ca;
        }

        caps = cap_init();

        // setting caps we should have in PERMITTED set
        if (cap_set_flag(caps, CAP_PERMITTED, cnt, cap_list, CAP_SET) == -1) {
            fmt::print(stderr, "Error  : problem with permitted capabilities. {}\n", srcpos.getSrcPos());
            if (debugflag)
                dump();
            exit(1);
        }

        if (cap_set_proc(caps) != 0) {
            fmt::print(stderr, "Error  : problem setting permitted capabilities.\n");
            if (debugflag)
                dump();
            cap_free(caps);
            exit(1);
        }

        cap_free(caps);
    }
#endif

    if (issetuid) {
        if (seteuid(uid)) {
            fmt::print(stderr, "Error  : can not change uid {}.\n", srcpos.getSrcPos());
            exit(1);
        }
    }
}

// remove a capability from the effective set
void Cap::lower_cap(std::vector<cap_value_t> cap_arg, int uid, utils::SrcPos srcpos) {
    if (traceflag) {
        fmt::println(stderr, "Trace  : Cap::lower_cap( {}, {})", uid, srcpos.getSrcPos());
        dump();
    }
#ifdef WS_CAPA
    if (hascaps) {
        cap_t caps;
        cap_value_t cap_list[cap_arg.size()]; // FIXME VLA not in ISO C++

        int cnt = 0;
        for (const auto& ca : cap_arg) {
            cap_list[cnt++] = ca;
        }

        caps = cap_get_proc();

        if (cap_set_flag(caps, CAP_EFFECTIVE, cnt, cap_list, CAP_CLEAR) == -1) {
            fmt::print(stderr, "Error  : problem with effective capabilities {}.\n", srcpos.getSrcPos());
            exit(1);
        }

        if (cap_set_proc(caps) == -1) {
            fmt::print(stderr, "Error  : problem lowering effective capabilities {}.\n", srcpos.getSrcPos());
            cap_t cap = cap_get_proc();
            fmt::print(stderr, "Info   : running with capabilities: {}\n", cap_to_text(cap, NULL));
            cap_free(cap);
            exit(1);
        }

        cap_free(caps);
    }
#endif

    if (issetuid) {
        if (seteuid(uid)) {
            fmt::print(stderr, "Error  : can not change uid {}.\n", srcpos.getSrcPos());
            exit(1);
        }
    }
}

// add a capability to the effective set
void Cap::raise_cap(std::vector<cap_value_t> cap_arg, utils::SrcPos srcpos) {
    if (traceflag) {
        fmt::println(stderr, "Trace  : Cap::raise_cap( {}, {})", cap_arg, srcpos.getSrcPos());
        dump();
    }
#ifdef WS_CAPA
    if (hascaps) {
        cap_t caps;
        cap_value_t cap_list[cap_arg.size()]; // FIXME VLA not in ISO C++

        int cnt = 0;
        for (const auto& ca : cap_arg) {
            cap_list[cnt++] = ca;
        }

        caps = cap_get_proc();

        if (cap_set_flag(caps, CAP_EFFECTIVE, cnt, cap_list, CAP_SET) == -1) {
            fmt::print(stderr, "Error  : problem with effective capabilities {}.\n", srcpos.getSrcPos());
            exit(1);
        }

        if (cap_set_proc(caps) == -1) {
            fmt::print(stderr, "Error  : problem raising effective capabilities. {}\n", srcpos.getSrcPos());
            cap_t cap = cap_get_proc();
            fmt::print(stderr, "Debug  : Running with capabilities: {}\n", cap_to_text(cap, NULL));
            cap_free(cap);
            exit(1);
        }

        cap_free(caps);
    }
#endif

    if (issetuid) {
        if (seteuid(0)) {
            fmt::print(stderr, "Error  : can not change uid {}.\n", srcpos.getSrcPos());
            exit(1);
        }
    }
}

// helper to dump current capabilities
void Cap::dump() const {
#ifdef WS_CAPA
    cap_t cap = cap_get_proc();
    char* cap_text = cap_to_text(cap, NULL);
    fmt::print(stderr, "Debug  : running with capabilities: {}\n", cap_text);
    cap_free(cap_text);
    cap_free(cap);
#endif
}
