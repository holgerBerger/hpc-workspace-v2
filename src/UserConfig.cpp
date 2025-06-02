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

#ifdef WS_RAPIDYAML_CONFIG
    #define RYML_USE_ASSERT 0
    #include "c4/format.hpp"
    #include "c4/std/std.hpp"
    #include "ryml.hpp"
    #include "ryml_std.hpp"
#else
    #include "yaml-cpp/yaml.h"
#endif

#include "user.h"
#include "utils.h"

#ifdef WS_RAPIDYAML_CONFIG

// TODO: FIXME: check if used correctly in the codebase!
// read user config from string (has to be read before dropping privileges)
//  unitest: yes
UserConfig::UserConfig(std::string userconf) {
    // get first line, this is either a mailaddress or something like key: value
    //  this is for compatibility with very old tools, which did not have a yaml file here
    // check if file looks like yaml
    if (userconf.find(":", 0) != string::npos) {

        ryml::Tree config = ryml::parse_in_place(ryml::to_substr(userconf)); // FIXME: error check?
        ryml::NodeRef node;
        ryml::NodeRef root = config.rootref();

        // TODO: type checks if nodes are of right type?

        readRyamlScalar(config, "mail", mailaddress);

        // if (node=config["mailaddress"]; node.has_val()) node>>mailaddress; else mailaddress="";
        if (node = config["groupname"]; node.has_val())
            node >> groupname;
        else
            groupname = "";
        if (root.has_child("duration")) {
            node = config["duration"];
            node >> duration;
        } else
            duration = -1;
        if (node = config["reminder"]; node.has_val())
            node >> reminder;
        else
            reminder = -1;
    } else {
        // get first line of userconf only that will include the mailaddress for reminder mails
        mailaddress = utils::getFirstLine(userconf);
    }

    if (!utils::isValidEmail(mailaddress)) {
        fmt::println(stderr, "Error  : invalid email address in ~/.ws_user.conf, ignored.");
        mailaddress = "";
    }
}

#else

// TODO: FIXME: check if used correctly in the codebase!
// read user config from string (has to be read before dropping privileges)
//  unitest: yes
UserConfig::UserConfig(std::string userconf) {
    YAML::Node user_home_config; // load yaml file from home here, not used anywhere else so far
    // get first line, this is either a mailaddress or something like key: value
    // std::getline(userconf, mailaddress);
    // check if file looks like yaml
    if (userconf.find(":", 0) != std::string::npos) {
        user_home_config = YAML::Load(userconf);
        if (user_home_config["mail"])
            mailaddress = user_home_config["mail"].as<std::string>();
        if (user_home_config["groupname"])
            groupname = user_home_config["groupname"].as<std::string>();
        if (user_home_config["duration"])
            duration = user_home_config["duration"].as<int>();
        if (user_home_config["reminder"])
            reminder = user_home_config["reminder"].as<int>();
    } else {
        // get first line of userconf only that will include the mailaddress for reminder mails
        mailaddress = utils::getFirstLine(userconf);
    }

    if (mailaddress != "" && !utils::isValidEmail(mailaddress)) {
        fmt::println(stderr, "Error  : invalid email address in ~/.ws_user.conf, ignored.");
        mailaddress = "";
    }
}
#endif
