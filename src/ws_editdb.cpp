/*
 *  hpc-workspace-v2
 *
 *  ws_editdb
 *
 *  tool to edit db entries, with patern matching, for admin only
 *
 *  c++ version of workspace utility
 *  a workspace is a temporary directory created in behalf of a user with a limited lifetime.
 *
 *  (c) Holger Berger 2021,2023,2024,2025
 *  (c) Christoph Niethammer 2025
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

#include <ctime>
#include <memory>
#include <optional>
#include <sstream>

#include "config.h"
#include <boost/program_options.hpp>

#include "build_info.h"
#include "db.h"
#include "fmt/base.h"
#include "fmt/ostream.h"
#include "fmt/ranges.h" // IWYU pragma: keep
#include "user.h"

#include "caps.h"
#include "utils.h"
#include "ws.h"

#include "spdlog/spdlog.h"

// init caps here, when euid!=uid
Cap caps{};

namespace po = boost::program_options;
using namespace std;

bool debugflag = false;
bool traceflag = false;
int debuglevel = 0;

// helper for fmt::
template <> struct fmt::formatter<po::options_description> : ostream_formatter {};

const long DAYS = 3600 * 24;

int main(int argc, char** argv) {

    // options and flags
    string filesystem;
    string user;
    string configfile;
    string pattern;
    string ensureuntil;
    string expireby;
    int addtime = 0;
    int addtimeexpired = 0;
    bool listexpired = false;
    bool dryrun = true;
    std::time_t date;
    // bool verbose = false;

    po::variables_map opts;

    // locals settings to prevent strange effects
    utils::setCLocal();

    // set custom logging format
    utils::setupLogging(string(argv[0]));

    // define options
    po::options_description cmd_options("\nOptions");
    // clang-format off
    cmd_options.add_options()
        ("help,h", "produce help message")
        ("version,V", "show version")
        ("filesystem,F", po::value<string>(&filesystem), "filesystem to list workspaces from")
        ("user,u", po::value<string>(&user), "only show workspaces for selected user")
        ("expired,e", "show expired workspaces")
        ("config", po::value<string>(&configfile), "config file")
        ("pattern,p", po::value<string>(&pattern), "pattern matching name (glob syntax)")
        ("dry-run", "dry-run (default), do nothing, just show what would be done")
        ("add-time", po::value<int>(&addtime), "add time to selected workspace expiration time, in days")
        ("add-time-expired", po::value<int>(&addtimeexpired), "add time to selected workspace expired time, in days")
        ("ensure-until", po::value<string>(&ensureuntil), "extend workspaces so that they expire not earlier than specified date (YYYY-MM-DD)")
        ("expire-by", po::value<string>(&expireby), "limit workspaces so that they expire no later than the specified date (YYYY-MM-DD)")
        ("not-kidding", "execute the actions")
        ("verbose,v", "verbose listing");
    // clang-format on

    po::options_description secret_options("Secret");
    secret_options.add_options()("debug", "show debugging information")("trace", "show tracing information");

    // define options without names
    po::positional_options_description p;
    p.add("pattern", 1);

    po::options_description all_options;
    all_options.add(cmd_options).add(secret_options);

    // parse commandline
    try {
        po::store(po::command_line_parser(argc, argv).options(all_options).positional(p).run(), opts);
        po::notify(opts);
    } catch (...) {
        fmt::print(stderr, "Usage: {} [options] [pattern]\n", argv[0]);
        fmt::println(stderr, "{}", cmd_options);
        exit(1);
    }

    // get flags

    listexpired = opts.count("expired");
    // verbose = opts.count("verbose");
    if (opts.count("dry-run") && opts.count("not-kidding")) {
        spdlog::error("Use either --dry-run or no-kidding.");
        exit(0);
    }

    if (opts.count("not-kidding")) {
        dryrun = false;
        spdlog::info("dry-run disabled");
    }

    // global flags
    debugflag = opts.count("debug");
    traceflag = opts.count("trace");

    // handle options exiting here

    if (opts.count("help")) {
        fmt::print(stderr, "Usage: ws_editdb [options] [pattern]\n");
        fmt::println(stderr, "{}", cmd_options);
        exit(0);
    }

    if (opts.count("version")) {
        utils::printVersion("ws_editdb");
        utils::printBuildFlags();
        exit(0);
    }

    // non root can use the tool on own config
    if (!user::isRoot() && configfile == "") {
        spdlog::warn("Sorry, this tool is for root only.");
        exit(-1);
    }

    // read config
    //   user can change this if no setuid installation OR if root
    auto configfilestoread = std::vector<cppfs::path>{"/etc/ws.d", "/etc/ws.conf"};
    if (configfile != "") {
        if (user::isRoot() || caps.isUserMode()) {
            configfilestoread = {configfile};
        } else {
            spdlog::warn("ignored config file options!");
        }
    }

    auto config = Config(configfilestoread);
    if (!config.isValid()) {
        spdlog::error("No valid config file found!");
        exit(-2);
    }

    if ((addtime != 0) + (addtimeexpired != 0) + !expireby.empty() + !ensureuntil.empty() > 1) {
        spdlog::error("Only one of add-time, add-time-expired, expire-by, or ensure-until can be specified!");
        exit(-2);
    }

    if (!expireby.empty() || !ensureuntil.empty()) {
        const std::string& datestr = !expireby.empty() ? fmt::format("{} 00:00:00",expireby) : fmt::format("{} 23:59:59", ensureuntil);

        std::tm tm = {};
        std::istringstream ss(datestr);
        ss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");

        if (ss.fail()) {
            spdlog::error("Date parsing failed for: {}", datestr);
            exit(-2);
        }

        tm.tm_isdst = -1;
        date = std::mktime(&tm);
        if (date == -1) {
            spdlog::error("Invalid date: {}", datestr);
            exit(-2);
        }
    }

    // root and admins can choose usernames
    string username = user::getUsername(); // used for rights checks
    string userpattern;                    // used for pattern matching in DB
    if (user::isRoot() || config.isAdmin(user::getUsername())) {
        if (user != "") {
            userpattern = user;
        } else {
            userpattern = "*";
        }
    } else {
        userpattern = username;
    }

    // if not pattern, show all entries
    if (pattern == "")
        pattern = "*";

    // where to list from?
    vector<string> fslist;
    vector<string> validfs = config.validFilesystems(username, {}, ws::LIST);
    if (filesystem != "") {
        if (canFind(validfs, filesystem)) {
            fslist.push_back(filesystem);
        } else {
            spdlog::error("invalid filesystem given.");
        }
    } else {
        fslist = validfs;
    }

    vector<std::unique_ptr<DBEntry>> entrylist;

    // iterate over filesystems and print or create list to be sorted
    for (auto const& fs : fslist) {
        if (debugflag)
            spdlog::debug("loop over fslist {} in {}", fs, fslist);
        std::unique_ptr<Database> db;
        try {
            db = std::unique_ptr<Database>(config.openDB(fs));
        } catch (DatabaseException& e) {
            spdlog::error(e.what());
            continue;
        }

#pragma omp parallel for schedule(dynamic)
        for (auto const& id : db->matchPattern(pattern, userpattern, {}, listexpired, false)) {
            try {
                std::unique_ptr<DBEntry> entry(db->readEntry(id, listexpired));
                // if entry is valid
                if (entry) {
#pragma omp critical
                    {
                        entrylist.push_back(std::move(entry));
                    }
                }
            } catch (DatabaseException& e) {
                spdlog::error(e.what());
            }
        }

    } // loop over fs

    if (dryrun)
        fmt::println("Actions that would be performed on the workspaces selected:");

    for (const auto& entry : entrylist) {
        if (debugflag){
            spdlog::debug("Id: {} ({})", entry->getId(), entry->getWSPath());
        }

        auto expiration = entry->getExpiration();
        std::optional<time_t> new_expiration;
        auto expired = entry->getExpired();
        std::optional<time_t> new_expired;

        if (addtime != 0) {
            new_expiration = expiration + (addtime * DAYS);
        } else if (!ensureuntil.empty()) {
            if (expiration < date) {
                new_expiration = date;
            }
        } else if (!expireby.empty()) {
            if (expiration > date) {
                new_expiration = date;
            }
        }

        if (addtimeexpired != 0) {
            new_expired = expired + (addtimeexpired * DAYS);
        }

        if (new_expiration.has_value()) {
            fmt::println("Id: {} ({})", entry->getId(), entry->getWSPath());
            time_t new_exp_value = new_expiration.value();
            auto olddate = utils::ctime(&expiration);
            auto newdate = utils::ctime(&new_exp_value);
            fmt::println("    change expiration: {} ({}) -> {} ({})", olddate, expiration, newdate, new_exp_value);
            if (!dryrun) {
                if (debugflag) {
                    spdlog::debug("updating entry");
                }
                entry->setExpiration(new_exp_value);
                entry->writeEntry();
            }
        }

        if (new_expired.has_value()) {
            fmt::println("Id: {} ({})", entry->getId(), entry->getWSPath());
            time_t new_exp_value = new_expired.value();
            auto olddate = utils::ctime(&expired);
            auto newdate = utils::ctime(&new_exp_value);
            fmt::println("    change expired: {} ({}) -> {} ({})", olddate, expired, newdate, new_exp_value);
            if (!dryrun) {
                if (debugflag) {
                    spdlog::debug("updating entry");
                }
                entry->setExpired(new_exp_value);
                entry->writeEntry();
            }
        }
    }
}
