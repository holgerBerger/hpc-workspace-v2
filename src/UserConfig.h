#pragma once
#ifndef CONFIG_USER_H
    #define CONFIG_USER_H
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

    #include <string>

// config in user home
class UserConfig {
  private:
    std::string mailaddress = ""; // default mailaddress for reminder
    std::string groupname = "";   // FIXME: TODO: does this make sense?
    int reminder = -1;            // days before expiration to send reminder email, send no mail if < 0
    int duration = -1;            // default duration for workspaces, request system default if < 0

  public:
    // read config from string, either YAML or single line
    UserConfig(std::string userconf);

    std::string getMailaddress() const { return mailaddress; };
    std::string getGroupname() const { return groupname; };
    int getReminder() const { return reminder; };
    int getDuration() const { return duration; };
};

#endif
