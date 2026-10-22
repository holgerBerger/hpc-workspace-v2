/*
 *  hpc-workspace-v2
 *
 *  ws_send_ical
 *
 *  -
 *
 *  c++ version of workspace utility
 *  a workspace is a temporary directory created in behalf of a user with a limited lifetime.
 *
 *  (c) Holger Berger 2021,2023,2024,2025
 *  (c) Maria Hampel 2025
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

#include "fmt/base.h"
#include "fmt/ostream.h"
#include "fmt/ranges.h" // IWYU pragma: keep
// #include <iostream>
#include <regex>
#include <string>

#include <syslog.h>
#include <time.h>
#include <unistd.h>

#include "UserConfig.h"
#include "build_info.h"
#include "caps.h"
#include "config.h"
#include "mail.h"
#include "user.h"
#include "utils.h"
#include "ws.h"

#include "spdlog/spdlog.h"

#include <boost/program_options.hpp>
namespace po = boost::program_options;
using namespace std;

// global variables
bool debugflag = false;
bool traceflag = false;
int debuglevel = 0;

// init caps here, when euid!=uid
Cap caps{};

template <> struct fmt::formatter<po::options_description> : ostream_formatter {};

std::string CRLF = "\r\n";

void commandline(po::variables_map& opt, string& filesystem, string& mailaddress, string& name, std::string& userconf,
                 std::string& configfile, std::string& username, int argc, char** argv) {

    // define all options
    po::options_description cmd_options("\nOptions");
    // clang-format off
    cmd_options.add_options()
        ("help,h", "produce help message")
        ("filesystem,F", po::value<string>(&filesystem), "filesystem where the workspace is located in")
        ("mail,m", po::value<string>(&mailaddress), "your mail address to send to")
        ("name,n", po::value<string>(&name), "name of selected workspace")
        ("username,u", po::value<string>(&username), "send the invitation for this user (root or admins only)")
        ("config", po::value<string>(&configfile), "config file");
    // clang-format on

    po::options_description secret_options("Secret");
    secret_options.add_options()("debug", "show debugging information")("trace", "show calling information");

    // define options without names
    po::positional_options_description p;
    p.add("name", 1);
    p.add("mail", 2);

    po::options_description all_options;
    all_options.add(cmd_options).add(secret_options);

    // parse commandline
    try {
        po::store(po::command_line_parser(argc, argv).options(all_options).positional(p).run(), opt);
        po::notify(opt);
    } catch (...) {
        fmt::println(stderr,
                     "Usage: {} [-F filesystem | --filesystem filesystem] [-n|--name] workspace_name [-m | --mail] "
                     "mailadress [-u | --username] username",
                     argv[0]);
        fmt::println(stderr, "{}", cmd_options);
        exit(1);
    }

    // help section
    if (opt.count("help")) {
        fmt::println(
            stderr,
            "Usage: [-F filesystem | --filesystem filesystem] [-n|--name] workspace_name [-m | --mail] mailadress [-u "
            "| --username] username",
            argv[0]);
        fmt::println(stderr, "{}", cmd_options);
        fmt::println(stderr, "this command is used to send a calendar invitation by Email to ensure users do not "
                             "forget\n the expiration date of a workspace");
        exit(0);
    }

    if (opt.count("version")) {
#ifdef IS_GIT_REPOSITORY
        fmt::println("workspace built from git commit hash {}  on top of release {}", GIT_COMMIT_HASH, WS_VERSION);
#else
        fmt::println("workspace version {}", WS_VERSION);
#endif
        exit(0);
    }

    // globalflags
    debugflag = opt.count("debug");
    traceflag = opt.count("trace");

    // userconfig for Mail
    UserConfig userconfig(userconf);

    // check if Workspace name is correctly formatted
    static const regex e("^[[:alnum:]][[:alnum:]_.-]*$");
    if (opt.count("name") && regex_match(name, e)) {
        // check if mail option is present and evaluate
        if (!opt.count("mail")) {
            mailaddress = userconfig.getMailaddress();

            if (mailaddress.length() > 0 && utils::isValidEmail(mailaddress)) {
                spdlog::info("Took email address <{}> from users config.", mailaddress);
            } else {
                spdlog::error("You can't use the ws_send_ical without a mailadress (-m).");
                exit(1);
            }
        } else {
            if (!utils::isValidEmail(mailaddress)) {
                spdlog::error("Invalid email address, abort");
                exit(1);
            }
        }
    } else {
        if (opt.count("name")) {
            spdlog::error("Illegal workspace name, use ASCII characters and numbers, '-','.' and '_' only!");
        } else {
            spdlog::error("no workspace name!");
        }
        fmt::println(
            stderr,
            "Usage: [-F filesystem | --filesystem filesystem] [-n|--name] workspace_name [-m | --mail] mailadress [-u "
            "| --username] username",
            argv[0]);
        fmt::println(stderr, "{}", cmd_options);
        fmt::println(stderr, "this command is used to send a calendar invitation by Email to ensure users do not "
                             "forget\n the expiration date of a workspace");
        exit(1);
    }
}

// Generate the Date Format used for ics attachments from time_t
std::string generateICSDateFormat(const time_t time) {
    char timeString[std::size("yyyymmddThhmmssZ")];
    struct tm tm_buf;
    localtime_r(&time, &tm_buf);
    std::strftime(std::data(timeString), std::size(timeString), "%Y%m%dT%H%M00Z", &tm_buf);
    std::string s(timeString);
    return s;
}

// Encode ics input for Base64
std::string base64Encode(const std::string& input) {
    const std::string chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result;
    unsigned int val = 0;
    int valb = -6;
    for (unsigned char c : input) {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0) {
            result.push_back(chars[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    if (valb > -6)
        result.push_back(chars[((val << 8) >> (valb + 8)) & 0x3F]);
    while (result.size() % 4)
        result.push_back('=');
    return result;
}

// Generate the ICS File
std::string generateICS(const std::unique_ptr<DBEntry>& entry, const std::string clustername, time_t createtime) {
    std::string wsname = entry->getId();
    time_t expirationtime = entry->getExpiration();
    std::string resource = entry->getFilesystem();
    time_t starttime = expirationtime - 7200;

    std::string starttimestr = generateICSDateFormat(starttime);
    std::string expirationtimestr = generateICSDateFormat(expirationtime);
    std::string createtimestr = generateICSDateFormat(createtime);

    std::size_t entryhash = std::hash<std::string>{}(entry->getId());

    std::stringstream ics;

    // HEADER
    ics << "BEGIN:VCALENDAR" << CRLF;
    ics << "VERSION:2.0" << CRLF;
    ics << "PRODID:-//HLRS Cluster Team//Workspace V2.1//EN" << CRLF; // ???
    ics << "METHOD:REQUEST" << CRLF;
    ics << "BEGIN:VTIMEZONE" << CRLF;
    ics << "TZID:Europe/Berlin" << CRLF;
    ics << "BEGIN:DAYLIGHT" << CRLF;
    ics << "TZOFFSETFROM:+0100" << CRLF;
    ics << "TZOFFSETTO:+0200" << CRLF;
    ics << "TZNAME:CEST" << CRLF;
    ics << "DTSTART:19700329T020000" << CRLF;
    ics << "RRULE:FREQ=YEARLY;BYDAY=-1SU;BYMONTH=3" << CRLF;
    ics << "END:DAYLIGHT" << CRLF;
    ics << "BEGIN:STANDARD" << CRLF;
    ics << "TZOFFSETFROM:+0200" << CRLF;
    ics << "TZOFFSETTO:+0100" << CRLF;
    ics << "TZNAME:CET" << CRLF;
    ics << "DTSTART:19701025T030000" << CRLF;
    ics << "RRULE:FREQ=YEARLY;BYDAY=-1SU;BYMONTH=10" << CRLF;
    ics << "END:STANDARD" << CRLF;
    ics << "END:VTIMEZONE" << CRLF;

    // EVENT
    ics << "BEGIN:VEVENT\r\n";
    ics << "CREATED:" << createtimestr << CRLF;
    ics << "DTSTAMP:" << createtimestr << CRLF;
    ics << "UID:587a1aa6-" << entryhash << CRLF;
    ics << "DESCRIPTION:Workspace " << wsname << " will be deleted on host " << clustername << CRLF;
    ics << "LOCATION:" << clustername << ":" << resource << CRLF;
    ics << "SUMMARY:Workspace " << wsname << " expires" << CRLF;
    ics << "DTSTART;TZID=Europe/Berlin:" << starttimestr << CRLF;
    ics << "DTEND;TZID=Europe/Berlin:" << expirationtimestr << CRLF;
    ics << "LAST_MODIFIED:" << createtimestr << CRLF;
    ics << "CLASS:PRIVATE" << CRLF;
    ics << "END:VEVENT" << CRLF;

    ics << "END:VCALENDAR" << CRLF;

    return ics.str();
}

// Generate the Mail
std::string generateMail(const std::unique_ptr<DBEntry>& entry, std::string ics, const std::string mail_from,
                         std::vector<std::string> mail_to, const std::string clustername, time_t now) {
    std::string wsname = entry->getId();
    std::string resource = entry->getFilesystem();

    std::stringstream mail;
    std::string expirationtimestr = mail::generateMailDateFormat(entry->getExpiration());
    std::string createtimestr = mail::generateMailDateFormat(now);
    std::string to_header = mail::generateToHeader(mail_to);

    std::string messageID = mail::generateMessageID("ws_send_ical");
    std::string boundary = "_NextPart_01234567.89ABCDEF";
    std::string encodedICS = base64Encode(ics);

    mail << "From: " << mail_from << CRLF;
    mail << "To: " << to_header << CRLF;
    mail << "Subject: Workspace expire on " << expirationtimestr << CRLF;
    mail << "Message-ID: <" << messageID << ">" << CRLF;
    mail << "Date: " << createtimestr << CRLF;
    mail << "MIME-Version: 1.0" << CRLF;
    mail << "Content-Type: multipart/mixed; boundary=" << boundary << CRLF;
    mail << "" << CRLF;

    mail << "--" << boundary << CRLF;
    mail << "Content-Type: text/plain; charset=UTF-8" << CRLF;
    mail << "Content-Transfer-Encoding: 7bit" << CRLF;
    mail << "" << CRLF;
    mail << "Your workspace " << wsname << " on filesystem " << resource << " at HPC System " << clustername
         << " is going to expire " << CRLF;
    mail << "" << CRLF;

    mail << "--" << boundary << CRLF;
    mail << "Content-Type: text/calendar; charset=UTF-8; method=REQUEST" << CRLF;
    mail << "Content-Transfer-Encoding: base64" << CRLF;
    mail << "Content-Disposition: attachment; filename=invite.ics" << CRLF;
    mail << "" << CRLF;

    for (size_t i = 0; i < encodedICS.length(); i += 76) {
        mail << encodedICS.substr(i, 76) << CRLF;
    }

    mail << "" << CRLF;
    mail << "--" << boundary << "--" << CRLF;
    mail << "" << CRLF;

    return mail.str();
}

int main(int argc, char** argv) {
    std::string filesystem;
    std::string mailaddress("");
    std::string name;
    std::string userconf;
    std::string configfile;

    std::string username; // selected by -u (root/admin only), empty means the calling user
    po::variables_map opt;

    // locals settings to prevent strange effects
    utils::setCLocal();

    // set custom logging format
    utils::setupLogging(string(argv[0]));

    // initiate time
    srand(time(nullptr));
    time_t now = time(nullptr);

    // setup curl
    mail::initCurl();

    // Get Userconf
    string user_conf_filename = user::getUserhome() + "/.ws_user.conf";
    if (!cppfs::is_symlink(user_conf_filename)) {
        if (cppfs::is_regular_file(user_conf_filename)) {
            userconf = utils::getFileContents(user_conf_filename.c_str());
        }
        // FIXME: could be parsed here and passed as object not string
    } else {
        spdlog::error("~/.ws_user.conf can not be symlink!");
        exit(-1);
    }

    // read commandlineoptions
    commandline(opt, filesystem, mailaddress, name, userconf, configfile, username, argc, argv);

    // Get Configfile
    auto configfilestoread = std::vector<cppfs::path>{"/etc/ws.d", "/etc/ws.conf"};
    if (configfile != "") {
        if (user::isRoot() || caps.isUserMode()) {
            configfilestoread = {configfile};
        } else {
            spdlog::warn("ignored config file option!");
        }
    }

    auto config = Config(configfilestoread);
    if (!config.isValid()) {
        spdlog::error("No valid config file found!");
        exit(-2);
    }

    // check if user is in debugusers list
    if (!config.isDebugUser(user::getUsername())) {
        if (debugflag || traceflag) {
            spdlog::warn("debug mode disabled, not in debugusers list");
            debugflag = false;
            traceflag = false;
        }
    }

    // root and admins can choose usernames
    string userpattern; // used for pattern matching in DB
    if (user::isRoot() || config.isAdmin(user::getUsername())) {
        if (username != "") {
            userpattern = username;
        } else {
            userpattern = "*";
        }
    } else {
        if (username != "" && username != user::getUsername()) {
            spdlog::warn("choosing a user with -u is only allowed for root and admins, using own username");
        }
        userpattern = user::getUsername();
    }

    auto grouplist = user::getGrouplist();
    bool listgroups = false; // don't allow notifications for Group Workspaces yet

    // Only search in valid Filesystems
    // NOTE: ACLs must be evaluated against the calling user (an empty name matches
    // no user_acl entry and would silently hide filesystems); admins pass anyway
    // because hasAccess() grants the admin list access to all filesystems
    vector<string> fslist;
    vector<string> validfs = config.validFilesystems(user::getUsername(), grouplist, ws::USE);
    if (filesystem != "") {
        if (canFind(validfs, filesystem)) {
            fslist.push_back(filesystem);
        } else {
            spdlog::error("invalid filesystem given.");
            exit(-3);
        }
    } else {
        fslist = validfs;
    }

    vector<std::unique_ptr<DBEntry>> entrylist;

    // iterate over filesystems and print or create list to be sorted
    for (auto const& fs : fslist) {
        if (debugflag)
            spdlog::debug("loop over fslist {} in {}\n", fs, fslist);

        std::unique_ptr<Database> db;
        try {
            db = std::unique_ptr<Database>(config.openDB(fs));
        } catch (DatabaseException& e) {
            spdlog::error(e.what());
            continue;
        }

        // catch DB access errors, if DB directory or DB is accessible
        try {
            for (auto const& id : db->matchPattern(name, userpattern, grouplist, false, listgroups)) {
                std::unique_ptr<DBEntry> entry(db->readEntry(id, false));
                if (entry) {
                    entrylist.push_back(std::move(entry)); // maybe incorrect use of std::unique_ptr
                }
            }
        } catch (DatabaseException& e) {
            spdlog::error(e.what());
            exit(-2);
        }
    } // loop fslist

    // If multiple entries exist force Filesystem option
    if (entrylist.size() > 1) {
        spdlog::error("multiple workspaces found, use the -F option to specify Filesystem");
        for (const std::unique_ptr<DBEntry>& entry : entrylist) {
            fmt::println(stderr, "Filesystem {}, Path {}", entry->getFilesystem(), entry->getWSPath());
        }
        exit(0);
    } else if (entrylist.empty()) {
        spdlog::warn("no workspace {} found, if you think there should be such a workspace, please check the output "
                     "of 'ws_restore -l' for removed workspaces which are possible to recover",
                     name);
        exit(1);
    } else {
        std::string mail_from = config.mailfrom();
        if (mail_from == "") {
            spdlog::warn("no mail_from in global config, please inform system administrator!");
            exit(-2);
        }
        std::string smtpUrl = "smtp://" + config.smtphost();
        std::string clustername = config.clustername();
        std::vector<std::string> mail_to;
        mail_to.push_back(mailaddress);

        const auto& entry = entrylist.front();

        std::string ics = generateICS(entry, clustername, now);
        if (debugflag) {
            spdlog::debug("Generated ICS content:");
            spdlog::debug("{}", ics);
        }
        // fmt::println(ics);

        std::string completeMail = generateMail(entry, ics, mail_from, mail_to, clustername, now);
        if (debugflag) {
            spdlog::debug("Generated email content:");
            spdlog::debug("{}", completeMail);
        }
        if (mail::sendCurl(smtpUrl, mail_from, mail_to, completeMail)) {
            spdlog::info("Success: Calendar invitation sent to {}", mailaddress);
        } else {
            spdlog::error("Failed to send calendar invitation to {}", mailaddress);
            exit(1);
        }

        // fmt::print("success; filesystem {}, workspacename {}, mailaddress {}", filesystem, name, mailaddress);
    }

    mail::cleanupCurl();
}
