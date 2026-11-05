/*
 *  hpc-workspace-v2
 *
 *  ws.cpp
 *
 *  - global helper functions
 *
 *  c++ version of workspace utility
 *  a workspace is a temporary directory created in behalf of a user with a limited lifetime.
 *
 *  (c) Christoph Niethammer 2026
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

#include "ws.h"

#include <algorithm>
#include <string>

namespace ws {

bool isValidWorkspaceName(const std::string& name) {
    // Implemented without std::regex
    if (name.empty()) {
        return false;
    }
    auto isAsciiAlnum = [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
    };
    if (!isAsciiAlnum(name.front())) {
        return false;
    }
    return std::all_of(name.begin() + 1, name.end(),
                       [&](unsigned char c) { return isAsciiAlnum(c) || c == '.' || c == '_' || c == '-'; });
}

} // namespace ws
