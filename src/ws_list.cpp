/*
 *  hpc-workspace-v2
 *
 *  ws_list
 *
 *  - tool to list workspaces
 *    changes to workspace++:
 *      - c++ implementation (not python anymore)
 *      - option to run in parallel (PARALLEL CMake flag),
 *        which helps to hide network latency of parallel filesystems
 *        needs as of 2024 openmp support
 *      - fast YAML reader with rapidyaml
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

#include <filesystem>
#include <memory>

#include "config.h"
#include <boost/program_options.hpp>

#include "build_info.h"
#include "db.h"
#include "fmt/base.h"
#include "fmt/color.h"
#include "fmt/format.h"
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

// print entry in traditional format, one below each other, multiline
void print_entry(const DBEntry* entry, const bool verbose, const bool terse, const bool permissions) {
    long remaining = entry->getExpiration() - time(0L);

    if (entry->getConfig()->isAdmin(user::getUsername())) {
        fmt::println("Id: {}", entry->getId());
    } else {
        fmt::println("Id: {}", utils::getID(user::getUsername(), entry->getId()));
    }

    fmt::println("    workspace directory  : {}", entry->getWSPath());
    if (remaining < 0) {
        fmt::println("    remaining time       : {}", "expired");
    } else {
        fmt::println("    remaining time       : {} days, {} hours", remaining / (24 * 3600),
                     (remaining % (24 * 3600)) / 3600);
    }
    if (!terse) {
        if (entry->getComment() != "")
            fmt::println("    comment              : {}", entry->getComment());
        if (entry->getCreation() > 0)
            fmt::println("    creation time        : {}", utils::ctime(entry->getCreation()));
        fmt::println("    expiration time      : {}", utils::ctime(entry->getExpiration()));
        if (entry->getExpired() > 0)
            fmt::println("    expired time         : {}", utils::ctime(entry->getExpired()));
        if (entry->getGroup() != "")
            fmt::println("    group                : {}", entry->getGroup());
        fmt::println("    filesystem name      : {}", entry->getFilesystem());
    }
    fmt::println("    available extensions : {}", entry->getExtension());
    if (verbose) {
        long rd = (entry->getExpiration() - (entry->getReminder() * 24 * 3600));
        fmt::println("    reminder             : {}", utils::ctime(&rd));
        if (entry->getMailaddress() != "")
            fmt::println("    mailaddress          : {}", entry->getMailaddress());
    }

    if (permissions) {
        auto perm = cppfs::status(entry->getWSPath()).permissions();
        fmt::println("    permissions          : {}", utils::permstring(perm));
    }
}

void print_entry_tableformat(const DBEntry* entry, [[maybe_unused]] const bool verbose, const bool terse,
                             [[maybe_unused]] const bool permissions) {
    static bool headerprinted = false;
    static bool color_checked = false;
    static bool color_output = true;

    long remaining = entry->getExpiration() - time(0L);

    std::string ID;
    if (entry->getConfig()->isAdmin(user::getUsername())) {
        ID = entry->getId();
    } else {
        ID = utils::getID(user::getUsername(), entry->getId());
    }

    if (!color_checked) {
        color_checked = true;
        const char* no_color = std::getenv("NO_COLOR");
        if (no_color != nullptr && no_color[0] != '\0')
            color_output = false;
        if (!isatty(STDOUT_FILENO))
            color_output = false;
    }

    fmt::color remaincolor;
    if (color_output) {
        if (remaining / (24 * 3600) < 3)
            remaincolor = fmt::color::red;
        else if (remaining / (24 * 3600) < 7)
            remaincolor = fmt::color::orange;
        else
            remaincolor = fmt::color::green;
    }

    if (terse) {
        if (!headerprinted) {
            headerprinted = true;
            fmt::println("{:<30} {:<50} {:<9}", "ID", "PATH", "REMAINING");
            fmt::println("{:=<30} {:=<50} {:=<9}", "", "", "");
        }
        if (color_output)
            fmt::println("{:<30} {:<50} {:<9}", ID, entry->getWSPath(),
                         fmt::styled(remaining / (24 * 3600), fg(remaincolor)));
        else
            fmt::println("{:<30} {:<50} {:<9}", ID, entry->getWSPath(), remaining / (24 * 3600));
    } else {
        if (!headerprinted) {
            headerprinted = true;
            fmt::println("{:<30} {:<50} {:<25} {:10} {:<9}", "ID", "PATH", "EXPIRATION", "EXTENSIONS", "REMAINING");
            fmt::println("{:=<30} {:=<50} {:=<25} {:=<10} {:=<9}", "", "", "", "", "");
        }
        if (color_output)
            fmt::println("{:<30} {:<50} {:<25} {:<10} {:<9}", ID, entry->getWSPath(),
                         utils::ctime(entry->getExpiration()), entry->getExtension(),
                         fmt::styled(remaining / (24 * 3600), fg(remaincolor)));
        else
            fmt::println("{:<30} {:<50} {:<25} {:<10} {:<9}", ID, entry->getWSPath(),
                         utils::ctime(entry->getExpiration()), entry->getExtension(), remaining / (24 * 3600));
    }
}

int main(int argc, char** argv) {

    // options and flags
    string filesystem;
    string user;
    string configfile;
    string pattern;
    bool listgroups = false;
    bool listfilesystems = false;
    bool listfilesystemdetails = false;
    bool shortlisting = false;
    bool listexpired = false;
    bool sortbyname = false;
    bool sortbycreation = false;
    bool sortbyremaining = false;
    bool sortreverted = false;
    bool terselisting = false;
    bool tableformat = false;
    bool permissions = false;
    bool verbose = false;

    po::variables_map opts;

    // locals settings to prevent strange effects
    utils::setCLocal();

    // setup logging
    utils::setupLogging(string(argv[0]));

    // define options
    po::options_description cmd_options("\nOptions");
    // clang-format off
    cmd_options.add_options()
        ("help,h", "produce help message")
        ("version,V", "show version")
        ("filesystem,F", po::value<string>(&filesystem), "filesystem to list workspaces from")
        ("group,g", "enable listing of group workspaces")
        ("listfilesystems,l", "list available filesystems")
        ("listfilesystemdetails,L", "list available filesystems with details")
        ("short,s", "short listing, only workspace names")
        ("user,u", po::value<string>(&user), "only show workspaces for selected user")
        ("expired,e", "show expired workspaces")
        ("name,N", "sort by name")
        ("creation,C", "sort by creation date")
        ("remaining,R", "sort by remaining time")
        ("reverted,r", "revert sort")
        ("terse,t", "terse listing")
        ("table,T", "table format")
        ("config", po::value<string>(&configfile), "config file")
        ("pattern,p", po::value<string>(&pattern), "pattern matching name (glob syntax)")
        ("permissions,P", "list permissions of workspace directory")
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

    listgroups = opts.count("group");
    listfilesystems = opts.count("listfilesystems");
    listfilesystemdetails = opts.count("listfilesystemdetails");
    shortlisting = opts.count("short");
    listexpired = opts.count("expired");
    sortbyname = opts.count("name");
    sortbycreation = opts.count("creation");
    sortbyremaining = opts.count("remaining");
    sortreverted = opts.count("reverted");
    terselisting = opts.count("terse");
    tableformat = opts.count("table");
    permissions = opts.count("permissions");
    verbose = opts.count("verbose");

    // global flags
    debugflag = opts.count("debug");
    traceflag = opts.count("trace");

    // handle options exiting here

    if (opts.count("help")) {
        fmt::print(stderr, "Usage: {} [options] [pattern]\n", argv[0]);
        fmt::println(stderr, "{}", cmd_options);
        exit(0);
    }

    if (opts.count("version")) {
        utils::printVersion("ws_list");
        utils::printBuildFlags();
        exit(0);
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

    // list of groups of this process
    auto grouplist = user::getGrouplist();

    // list of fileystems or list of workspaces
    if (listfilesystems) { // -l
        fmt::print("available filesystems (sorted according to priority):\n");
        for (auto fs : config.validFilesystems(username, grouplist, ws::LIST)) {
            fmt::print("{}\n", fs);
        }
    } else if (listfilesystemdetails) { // -L
        fmt::println("available filesystems (sorted according to priority):\n");
        fmt::println("{:>10}{:>12}{:>12}{:>10}{:>12}{:>12}{:>12}{:>10}", "name", "maxduration", "extensions",
                     "keeptime", "allocatable", "extendable", "restorable", "comment");
        for (auto fs : config.validFilesystems(username, grouplist, ws::LIST)) {
            auto fsc = config.getFsConfig(fs);
            bool allocateable = config.hasAccess(username, grouplist, fs, ws::CREATE) && fsc.allocatable;
            bool extendable = config.hasAccess(username, grouplist, fs, ws::EXTEND) && fsc.extendable;
            bool restorable = config.hasAccess(username, grouplist, fs, ws::RESTORE) && fsc.restorable;
            fmt::print("{:>10}{:>12}{:>12}{:>10}{:>12}{:>12}{:>12}   {}\n", fs, fsc.maxduration, fsc.maxextensions,
                       fsc.keeptime, allocateable, extendable, restorable, fsc.comment);
        }

        if (verbose) {
            fmt::println("\nExplanation:");
            fmt::println("  maxduration: maximum number of days you can specify with ws_allocate as lifetime");
            fmt::println("   extensions: the numer of times you can extend the workspace");
            fmt::println(
                "     keeptime: the number of days an expired or released workspace can be restored (unless data "
                "was deleted)");
            fmt::println("  allocatable: flag if new workspaces can be created here");
            fmt::println("   extendable: flag if workspaces can be extended here");
            fmt::println("   restorable: flag if new workspaces can be restored here");
        }
    } else { // list workspaces

        bool sort = sortbyname || sortbycreation || sortbyremaining;

        // if not pattern, show all entries
        if (pattern == "")
            pattern = "*";

        // where to list from?
        vector<string> fslist;
        vector<string> validfs = config.validFilesystems(username, grouplist, ws::LIST);
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
        vector<std::unique_ptr<Database>> dblist;
        std::unique_ptr<Database> db;

        // iterate over filesystems and print or create list to be sorted
        for (auto const& fs : fslist) {
            if (debugflag)
                spdlog::debug("loop over fslist {} in {}", fs, fslist);

            try {
                db = std::unique_ptr<Database>(config.openDB(fs));
            } catch (DatabaseException& e) {
                spdlog::error(e.what());
                continue;
            }

            // get list before loop to prevent matching per thread
            auto matchlist = db->matchPattern(pattern, userpattern, grouplist, listexpired, listgroups);

#pragma omp parallel for schedule(dynamic)
            for (auto const& id : matchlist) {
                try {
                    std::unique_ptr<DBEntry> entry(db->readEntry(id, listexpired));
                    // if entry is valid
                    if (entry) {
#pragma omp critical
                        {
                            // if no sorting, print, otherwise append to list
                            if (!sort) {
                                if (shortlisting) {
                                    fmt::println(entry->getId()); // FIXME:: correct for root/admin?
                                } else {
                                    if (!tableformat)
                                        print_entry(entry.get(), verbose, terselisting, permissions);
                                    else
                                        print_entry_tableformat(entry.get(), verbose, terselisting, permissions);
                                }
                            } else {
                                entrylist.push_back(std::move(entry));
                            }
                        }
                    }
                } catch (DatabaseException& e) {
                    spdlog::error(e.what());
                }
            }

            dblist.push_back(std::move(db));

        } // loop over fs

        // in case of sorted output, sort and print here
        if (sort) {
            if (debugflag)
                spdlog::debug("sorting remaining={},creation={},name={},reverse={}", sortbyremaining, sortbycreation,
                              sortbyname, sortreverted);
            if (sortbyremaining)
                std::sort(entrylist.begin(), entrylist.end(),
                          [](const auto& x, const auto& y) { return (x->getRemaining() < y->getRemaining()); });
            if (sortbycreation)
                std::sort(entrylist.begin(), entrylist.end(),
                          [](const auto& x, const auto& y) { return (x->getCreation() < y->getCreation()); });
            if (sortbyname)
                std::sort(entrylist.begin(), entrylist.end(),
                          [](const auto& x, const auto& y) { return (x->getId() < y->getId()); });

            if (sortreverted) {
                std::reverse(entrylist.begin(), entrylist.end());
            }

            for (const auto& entry : entrylist) {
                if (shortlisting) {
                    fmt::println(entry->getId()); // FIXME:: correct for root/admin?
                } else {
                    if (!tableformat)
                        print_entry(entry.get(), verbose, terselisting, permissions);
                    else
                        print_entry_tableformat(entry.get(), verbose, terselisting, permissions);
                }
            }
        }
    }
}
