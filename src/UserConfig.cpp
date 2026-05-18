/*
 *  hpc-workspace-v2
 *
 *  UserConfig.cpp
 *
 *  - deals with user configuration file
 *
 *  c++ version of workspace utility
 *  a workspace is a temporary directory created in behalf of a user with a limited lifetime.
 *
 *  (c) Holger Berger 2021,2023,2024
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

#include "UserConfig.h"

#include <glaze/glaze.hpp>
#include <glaze/yaml.hpp>

#include "user.h"
#include "utils.h"

#include "spdlog/spdlog.h"

struct UserConfig_GLZ {
    std::string mail;
    std::string groupname = "";
    int duration = -1;
    int reminder = -1;
};

template <> struct glz::meta<UserConfig_GLZ> {
    using T = UserConfig_GLZ;
    static constexpr auto value =
        glz::object("mail", &T::mail, "groupname", &T::groupname, "duration", &T::duration, "reminder", &T::reminder);
};

// read user config from string (has to be read before dropping privileges)
//  unittest: yes
UserConfig::UserConfig(std::string userconf) {
    // get first line, this is either a mailaddress or something like key: value
    //  this is for compatibility with very old tools, which did not have a yaml file here
    // check if file looks like yaml
    if ((userconf.find(":", 0) != std::string::npos) || (userconf[0] == '#')) {
        UserConfig_GLZ ucfg{};
        auto ec = glz::read<glz::opts{.format = glz::YAML}>(ucfg, userconf);
        if (!ec) {
            mailaddress = ucfg.mail;
            groupname = ucfg.groupname;
            duration = ucfg.duration;
            reminder = ucfg.reminder;
        } else {
            // YAML parse failed, fall through to bare-email mode
            mailaddress = utils::getFirstLine(userconf);
        }
    } else {
        // bare email mode
        mailaddress = utils::getFirstLine(userconf);
    }

    if (!utils::isValidEmail(mailaddress)) {
        spdlog::error("invalid email address in ~/.ws_user.conf, ignored.");
        mailaddress = "";
    }
}
