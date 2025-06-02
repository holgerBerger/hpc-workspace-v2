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

#include "utils.h"

#ifdef WS_CAPA
    #include <sys/capability.h>
#else
typedef int cap_value_t;
const int CAP_CHOWN = 0;
const int CAP_DAC_OVERRIDE = 1;
const int CAP_DAC_READ_SEARCH = 2;
const int CAP_FOWNER = 3;
#endif

class Cap {
  private:
    bool hascaps;
    bool issetuid;
    bool isusermode;

  public:
    Cap();

    // functions that can be called to raise and lower caps
    void drop_caps(std::vector<cap_value_t> cap_arg, int uid, utils::SrcPos srcpos);
    void lower_cap(std::vector<cap_value_t> cap_arg, int dbuid, utils::SrcPos);
    void raise_cap(std::vector<cap_value_t> cap_arg, utils::SrcPos);

    bool isSetuid() { return issetuid; };
    bool hasCaps() { return hascaps; };
    bool isUserMode() { return isusermode; };

    void dump() const;
};

#endif
