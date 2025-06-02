#ifndef WS_H
#define WS_H
/*
 *  hpc-workspace-v2
 *
 *  ws.h
 *
 *  - global types and defines
 *
 *  c++ version of workspace utility
 *  a workspace is a temporary directory created in behalf of a user with a limited lifetime.
 *
 *  (c) Holger Berger 2025
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

#include <map>

namespace ws {

// intent for ACLs
enum intent { LIST, USE, CREATE, EXTEND, RELEASE, RESTORE };

// get enum for name, for parsing ACLs
const std::map<std::string, int> intentnames = {{"list", LIST},     {"use", USE},         {"create", CREATE},
                                                {"extend", EXTEND}, {"release", RELEASE}, {"restore", RESTORE}};
} // namespace ws

#endif
