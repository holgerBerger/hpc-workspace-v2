/*
 *  hpc-workspace-v2
 *
 *  ws_expirer
 *
 *  - tool to expire and delete workspaces from a cronjob
 *    changes to workspace++:
 *      - c++ implementation (not python anymore)
 *
 *  c++ version of workspace utility
 *  a workspace is a temporary directory created in behalf of a user with a limited lifetime.
 *
 *  (c) Holger Berger 2021,2023,2024,2025,2026
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

#include <algorithm>
#include <exception>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "config.h"
#include <boost/program_options.hpp>

#include "build_info.h"
#include "db.h"
#include "fmt/base.h"
#include "fmt/ostream.h"
#include "fmt/ranges.h" // IWYU pragma: keep
#include "user.h"

#include "UserConfig.h"
#include "caps.h"
#include "config.h"
#include "mail.h"
#include "user.h"
#include "utils.h"

#include "spdlog/sinks/daily_file_sink.h" // IWYU pragma: keep
#include "spdlog/sinks/stdout_color_sinks.h"
#include "spdlog/sinks/syslog_sink.h" // IWYU pragma: keep
#include "spdlog/spdlog.h"

// init caps here, when euid!=uid
Cap caps{};

namespace po = boost::program_options;
namespace cppfs = std::filesystem;

// helper for fmt::
template <> struct fmt::formatter<po::options_description> : ostream_formatter {};

using namespace std;

bool debugflag = false;
bool traceflag = false;
int debuglevel = 0;

bool cleanermode = false;
bool forcedeletereleased = false;

// type for statistics
struct expire_result_t {
    long active_seen;
    long active_keep;
    long active_expired;
    long active_mails;

    long inactive_seen;
    long inactive_keep;
    long inactive_deleted;

    // add elements for global sum
    expire_result_t& operator+=(const expire_result_t& other) {
        active_seen += other.active_seen;
        active_keep += other.active_keep;
        active_expired += other.active_expired;
        active_mails += other.active_mails;

        inactive_seen += other.inactive_seen;
        inactive_keep += other.inactive_keep;
        inactive_deleted += other.inactive_deleted;
        return *this; // Return a reference to the modified object
    }
};

// type for statistics
struct clean_stray_result_t {
    long valid_ws;
    long invalid_ws;
    long valid_deleted;
    long invalid_deleted;

    // add elements for global sum
    clean_stray_result_t& operator+=(const clean_stray_result_t& other) {
        valid_ws += other.valid_ws;
        invalid_ws += other.invalid_ws;
        valid_deleted += other.valid_deleted;
        invalid_deleted += other.invalid_deleted;
        return *this; // Return a reference to the modified object
    }
};

// type for morbid db files
struct morbid_db_files_t {
    long count;
    std::vector<std::pair<std::string, std::string>> idreason;

    morbid_db_files_t& operator+=(morbid_db_files_t other) {
        count += other.count;
        idreason.insert(idreason.end(), other.idreason.begin(), other.idreason.end());
        return *this; // Return a reference to the modified object
    }

    void add(const std::pair<std::string, std::string> pair) {
        count += 1;
        idreason.emplace_back(pair);
    }
};

const std::string CRLF = "\r\n";
const std::string boundary = "_NextPart_01234567.89ABCDEF";

// Format a time difference in seconds as a fixed-width string with 3 digits and appropriate unit
static std::string formatTimedelta(long seconds) {
    if (seconds < 0)
        seconds = 0;

    long days = seconds / 86400;
    long hours = (seconds % 86400) / 3600;
    long minutes = (seconds % 3600) / 60;

    if (days > 0) {
        return fmt::format("{:3} days   ", days);
    } else if (hours > 0) {
        return fmt::format("{:3} hours  ", hours);
    } else {
        return fmt::format("{:3} minutes", minutes > 0 ? minutes : 1);
    }
}

// own ws_expirer logging setup,
// logs in color to console
static void setupMinimalLogging() {
    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    console_sink->set_pattern("%^%l%$: %v");

    spdlog::logger* log = new spdlog::logger("ws_expirer", {console_sink});
    spdlog::set_default_logger(std::shared_ptr<spdlog::logger>(log));
    spdlog::set_level(spdlog::level::trace);
}

// own ws_expirer logging setup,
// logs in color to console
// and into a daily rotating file with timestamps if name is provided
static void setupLogging(const std::string pathname) {
    if (pathname.size() == 0) {
        spdlog::warn("config contains no expirerlogpath, no file logging.");
        return; // this early return keeps logging setup before in place
    }

    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    console_sink->set_pattern("%^%l%$: %v");

    auto file_sink = std::make_shared<spdlog::sinks::daily_file_format_sink_mt>(pathname, 0, 1);
    file_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");
    spdlog::logger* log = new spdlog::logger("ws_expirer", {file_sink, console_sink});
    spdlog::set_default_logger(std::shared_ptr<spdlog::logger>(log));
    spdlog::set_level(spdlog::level::trace);
}

// file rename that falls back to utils::mv in case of EXDEV
void robust_rename(const cppfs::path src, const cppfs::path dest) {
    try {
        cppfs::rename(src, dest);
    } catch (cppfs::filesystem_error& e) {
        if (e.code() == std::errc::cross_device_link) {
            spdlog::info("cross device rename, falling back to 'mv'");
            int ret = utils::mv(src.c_str(), dest.c_str());
            if (ret != 0) {
                spdlog::error("mv failed with code {}, errno: {} ({})", ret, errno, strerror(errno));
            }
        }
    }
}

// construct reminder mail (does not send it)
std::string generateReminderMail(const std::string& mail_from, std::vector<std::string>& mail_to,
                                 const long expirationtime, const std::string& wsname, const std::string& fsname,
                                 const std::string& clustername) {

    std::stringstream mail;
    std::string expirationtimestr = mail::generateMailDateFormat(expirationtime);
    std::string messageID = mail::generateMessageID("ws_expirer");
    std::string createtimestr = mail::generateMailDateFormat(time((long*)0L));

    std::string to_header = mail::generateToHeader(mail_to);

    mail << "From: " << mail_from << CRLF;
    mail << "To: " << to_header << CRLF;
    mail << "Subject: Workspace " << wsname << " will expire at " << expirationtimestr << CRLF;
    mail << "Message-ID: <" << messageID << ">" << CRLF;
    mail << "Date: " << createtimestr << CRLF;
    mail << "MIME-Version: 1.0" << CRLF;
    mail << "Content-Type: multipart/mixed; boundary=" << boundary << CRLF;
    mail << "" << CRLF;

    mail << "--" << boundary << CRLF;
    mail << "Content-Type: text/plain; charset=UTF-8" << CRLF;
    mail << "Content-Transfer-Encoding: 7bit" << CRLF;
    mail << "" << CRLF;
    mail << "Your workspace " << wsname << " on filesystem " << fsname << " at HPC System " << clustername
         << " will expire at " << expirationtimestr << CRLF;
    mail << "" << CRLF;

    mail << "" << CRLF;
    mail << "--" << boundary << "--" << CRLF;
    mail << "" << CRLF;

    return mail.str();
}

// construct expiration mail for user
std::string generateExpirationMail(const std::string& mail_from, std::vector<std::string>& mail_to,
                                   const long expirationtime, const std::string& wsname, const std::string& fsname,
                                   const std::string& clustername, const int keeptime) {

    std::stringstream mail;
    std::string expirationtimestr = mail::generateMailDateFormat(expirationtime);
    std::string messageID = mail::generateMessageID("ws_expirer");
    std::string createtimestr = mail::generateMailDateFormat(time((long*)0L));

    std::string to_header = mail::generateToHeader(mail_to);

    mail << "From: " << mail_from << CRLF;
    mail << "To: " << to_header << CRLF;
    mail << "Subject: Workspace " << wsname << " expired at " << expirationtimestr << CRLF;
    mail << "Message-ID: <" << messageID << ">" << CRLF;
    mail << "Date: " << createtimestr << CRLF;
    mail << "MIME-Version: 1.0" << CRLF;
    mail << "Content-Type: multipart/mixed; boundary=" << boundary << CRLF;
    mail << "" << CRLF;

    mail << "--" << boundary << CRLF;
    mail << "Content-Type: text/plain; charset=UTF-8" << CRLF;
    mail << "Content-Transfer-Encoding: 7bit" << CRLF;
    mail << "" << CRLF;
    mail << "Your workspace " << wsname << " on filesystem " << fsname << " at HPC System " << clustername
         << " was expired at " << expirationtimestr << CRLF;
    mail << "Your workspace can be restored for the next " << keeptime
         << " days using ws_restore, to view restorable workspaces use ws_restore -l or ws_list -e" << CRLF;
    mail << "" << CRLF;
    mail << "(you can disable this email by setting 'expirationmail: false' in your ~/.ws_user.conf)" << CRLF;

    mail << "" << CRLF;
    mail << "--" << boundary << "--" << CRLF;
    mail << "" << CRLF;

    return mail.str();
}

// construct error mail (does not send it)
std::string generateErrorMail(const std::string& mail_from, std::vector<std::string> mail_to,
                              const std::string& subject) {
    std::stringstream mail;
    std::string messageID = mail::generateMessageID("ws_expirer");
    std::string createtimestr = mail::generateMailDateFormat(time((long*)0L));

    std::string to_header = mail::generateToHeader(mail_to);

    mail << "From: " << mail_from << CRLF;
    mail << "To: " << to_header << CRLF;
    mail << "Subject: " << subject << CRLF;
    mail << "Message-ID: <" << messageID << ">" << CRLF;
    mail << "Date: " << createtimestr << CRLF;
    mail << "MIME-Version: 1.0" << CRLF;
    mail << "Content-Type: multipart/mixed; boundary=" << boundary << CRLF;
    mail << "" << CRLF;

    mail << "--" << boundary << CRLF;
    mail << "Content-Type: text/plain; charset=UTF-8" << CRLF;
    mail << "Content-Transfer-Encoding: 7bit" << CRLF;
    mail << "" << CRLF;
    mail << subject << "\nSkipping to avoid data loss. Please check Filesystem!" << CRLF;
    mail << "" << CRLF;

    mail << "" << CRLF;
    mail << "--" << boundary << "--" << CRLF;
    mail << "" << CRLF;

    return mail.str();
}

std::string generateSummaryMail(const std::string& mail_from, std::vector<std::string> mail_to,
                                const std::string subject, const std::string& body) {

    std::stringstream mail;
    std::string messageID = mail::generateMessageID("ws_expirer");
    std::string createtimestr = mail::generateMailDateFormat(time((long*)0L));

    std::string to_header = mail::generateToHeader(mail_to);

    mail << "From: " << mail_from << CRLF;
    mail << "To: " << to_header << CRLF;
    mail << "Subject: " << subject << CRLF;
    mail << "Message-ID: <" << messageID << ">" << CRLF;
    mail << "Date: " << createtimestr << CRLF;
    mail << "MIME-Version: 1.0" << CRLF;
    mail << "Content-Type: multipart/mixed; boundary=" << boundary << CRLF;
    mail << "" << CRLF;

    mail << "--" << boundary << CRLF;
    mail << "Content-Type: text/plain; charset=UTF-8" << CRLF;
    mail << "Content-Transfer-Encoding: 7bit" << CRLF;
    mail << "" << CRLF;
    mail << subject << CRLF;
    mail << "" << CRLF;
    mail << body << CRLF;

    mail << "" << CRLF;
    mail << "--" << boundary << "--" << CRLF;
    mail << "" << CRLF;

    return mail.str();
}

// clean_stray_directories
//  finds directories that are not in DB and removes them,
//  returns numbers of valid and invalid directories
//  this searches over filesystem and compares with DB, checks if a valid DB is available (using a magic file)
static clean_stray_result_t clean_stray_directories(const Config& config, const std::string fs,
                                                    const std::string single_space, const bool dryrun) {

    clean_stray_result_t result = {0, 0, 0, 0};

    // helper to store space and found dir together
    struct dir_t {
        std::string space;
        std::string dir;
    };

    std::vector<string> spaces = config.getFsConfig(fs).spaces;
    std::vector<dir_t> dirs; // list of all directories in all spaces of 'fs'

    // Infos needed to send errormails
    std::string mail_from = config.mailfrom();
    std::vector<std::string> adminmails = config.adminmail();
    std::string smtpUrl = "smtp://" + config.smtphost();

    //////// stray directories /////////
    // move directories not having a DB entry to deleted

    spdlog::info("* STRAY DIRECTORY REMOVAL for filesystem: {}", fs);
    spdlog::info("   workspaces first...");

    if (single_space != "") {
        if (canFind(spaces, single_space)) {
            spdlog::info("   only cleaning in space {}", single_space);
            spaces = {single_space};
        } else {
            spdlog::info("   given space not in filesystem {}, skipping.", fs);
            return {0, 0, 0, 0};
        }
    }

    // find directories first, check DB entries later, to prevent data race with
    // workspaces getting created while this is running
    // also collect non-matching directories for logging
    std::vector<string> non_matching_dirs;
    // Filter out the deleted directory path configured for the filesystem
    const std::string deletedPath = config.deletedPath(fs);
    for (const auto& space : spaces) {
        // NOTE: *-* for compatibility with old expirer
        // collect all directories first to separate matching and non-matching
        for (const auto& entry : utils::dirEntries(space, "*", true)) {
            if (cppfs::is_directory(cppfs::path(space) / entry)) {
                if (entry.find('-') != string::npos) {
                    dirs.push_back({space, entry});
                } else {
                    if (entry != deletedPath) {
                        non_matching_dirs.push_back(entry);
                    }
                }
            }
        }
    }
    // Log non-matching directories for manual intervention

    if (!non_matching_dirs.empty()) {
        spdlog::warn(" Found {} directories not matching pattern '*-*' in filesystem {}: ", non_matching_dirs.size(),
                     fs);
        for (const auto& dir : non_matching_dirs) {
            spdlog::warn("    {}", dir);
        }
        spdlog::warn(" These directories will be ignored and require manual intervention.");
    }

    // check for errors, if this throws DB is invalid and we should skip this DB
    std::unique_ptr<Database> db;
    try {
        db = std::unique_ptr<Database>(config.openDB(fs));
    } catch (DatabaseException& e) {
        spdlog::error(e.what());
        spdlog::error("skipping, to avoid data loss");

        // Sending Error Mail
        std::string subject = e.what();

        if (smtpUrl == "" || mail_from == "" || adminmails.size() == 0) {
            spdlog::warn(
                "No smtphost or mailfrom available to contact users or admins, please check your system config");
        } else {
            std::string completeMail = generateErrorMail(mail_from, adminmails, subject);
            try {
                if (!mail::sendCurl(smtpUrl, mail_from, adminmails, completeMail)) {
                    spdlog::error("Failed to send email, please check the mailaddress in the DB Entry");
                }
            } catch (const std::exception& e) {
                spdlog::error("Exception while sending email: {}", e.what());
            }
        }

        return result;
    }

    // get all workspace pathes from DB
    // this is a list of all workspace paths in the DB, used to compare with the filesystem
    auto wsIDs = db->matchPattern("*", "*", {}, false, false);       // (1)
    std::vector<std::pair<std::string, std::string>> workspacesInDB; // pair of (id, wspath)

    workspacesInDB.reserve(wsIDs.size());
    for (auto const& wsid : wsIDs) {
        try {
            workspacesInDB.push_back(std::make_pair(wsid, db->readEntry(wsid, false)->getWSPath()));
        } catch (const std::exception& e) {
            workspacesInDB.push_back(std::make_pair(wsid, "")); // store empty path for failed entries, but keep the id!
            spdlog::warn("    failed to read DB entry {}: {}", wsid, e.what());
            // TODO: is that something to inform admin about? this workspace is immortal!
        }
    }

    // compare filesystem with DB
    for (auto const& founddir : dirs) { // (2)
        if (std::none_of(workspacesInDB.begin(), workspacesInDB.end(), [&](const auto& item) {
                // fmt::println("{} == {} || {} == {}", item.second, (cppfs::path(founddir.space) /
                // cppfs::path(founddir.dir)).string(), item.first, cppfs::path(founddir.dir).string());
                return item.second == (cppfs::path(founddir.space) / cppfs::path(founddir.dir)).string() ||
                       item.first == cppfs::path(founddir.dir).string();
            })) {
            spdlog::warn("    stray workspace {}", founddir.dir);

            // FIXME: a stray workspace will be moved to deleted here, and will be deleted in
            // the same run in (3). Is this intended? dangerous with datarace #87

            string timestamp = fmt::format("{}", time(NULL));
            spdlog::info("         {}move {} to {}", cleanermode ? "" : "would ",
                         (cppfs::path(founddir.space) / founddir.dir).string(),
                         (cppfs::path(founddir.space) / config.deletedPath(fs) /
                          (cppfs::path(founddir.dir).filename().string() + "-" + timestamp))
                             .string());
            if (!dryrun) {
                try {
                    robust_rename(cppfs::path(founddir.space) / founddir.dir,
                                  cppfs::path(founddir.space) / config.deletedPath(fs) /
                                      (cppfs::path(founddir.dir).filename().string() + "-" + timestamp));
                } catch (cppfs::filesystem_error& e) {
                    spdlog::error("       failed to move to deleted: {} ({})", founddir.dir, e.what());
                }
            }
            result.invalid_ws++;
        } else {
            spdlog::info("       found valid workspace: {}", founddir.dir);
            result.valid_ws++;
        }
    }

    workspacesInDB.clear(); // clear old data

    spdlog::info("    {} valid, {} invalid directories found.", result.valid_ws, result.invalid_ws);

    spdlog::info("   ... deleted workspaces second...");

    ///// DELETED workspaces /////  (3)
    // delete deleted workspaces that no longer have any DB entry
    // ws_release moves DB entry first, workspace second, should be race free
    dirs.clear();
    // directory entries first
    for (auto const& space : spaces) {
        // NOTE: *-* for compatibility with old expirer
        for (const auto& dir : utils::dirEntries(cppfs::path(space) / config.deletedPath(fs), "*-*", true)) {
            if (cppfs::is_directory(cppfs::path(space) / config.deletedPath(fs) / dir)) {
                dirs.push_back({space, dir});
            }
        }
    }

    // get all workspace names from DB, this contains the timestamp
    wsIDs = db->matchPattern("*", "*", {}, true, false);
    std::vector<std::string> workspacesInDeletedDB;
    workspacesInDeletedDB.reserve(wsIDs.size());
    for (auto const& wsid : wsIDs) {
        workspacesInDeletedDB.push_back(wsid);
    }

    // compare filesystem with DB
    for (auto const& founddir : dirs) {
        if (!canFind(workspacesInDeletedDB, founddir.dir)) {
            spdlog::warn("    stray removed workspace {}", founddir.dir);
            spdlog::info("      {}remove {}", cleanermode ? "" : "would ",
                         (cppfs::path(founddir.space) / config.deletedPath(fs) / founddir.dir).string());
            if (!dryrun) {
                try {
                    // timeout is now + deldirtimeout
                    std::time_t deadline = std::time_t(std::time_t(nullptr)) + config.deldirtimeout();

                    utils::rmtree(cppfs::path(founddir.space) / config.deletedPath(fs) / founddir.dir, deadline);

                } catch (cppfs::filesystem_error& e) {
                    spdlog::error("      failed to remove: {} ({})",
                                  (cppfs::path(founddir.space) / config.deletedPath(fs) / founddir.dir).string(),
                                  e.what());
                }
            }
            result.invalid_deleted++;
        } else {
            spdlog::info("       found valid workspace: {}", founddir.dir);
            result.valid_deleted++;
        }
    }

    spdlog::info(" =>   {} valid expired, {} invalid expired directories found.", result.valid_deleted,
                 result.invalid_deleted);
    return result;
}

// expire workspace DB entries and moves the workspace to deleted directory
// deletes expired workspace in second phase
static expire_result_t expire_workspaces(const Config& config, const string fs, const bool dryrun,
                                         morbid_db_files_t& morbid_db_files) {

    expire_result_t result = {0, 0, 0, 0, 0, 0, 0};

    // Infos needed for errormails and remindermails
    std::string smtpUrl = "smtp://" + config.smtphost();
    std::string mail_from = config.mailfrom();
    std::vector<std::string> adminmails = config.adminmail();

    // vector<string> spaces = config.getFsConfig(fs).spaces;

    std::unique_ptr<Database> db;
    // check for errors, if this throws DB is invalid and we should skip this DB
    try {
        db = std::unique_ptr<Database>(config.openDB(fs));
    } catch (DatabaseException& e) {
        spdlog::error(e.what());
        spdlog::error("skipping, to avoid data loss");

        // Sending Error Mail
        std::string subject = e.what();

        if (smtpUrl == "" || mail_from == "" || adminmails.size() == 0) {
            spdlog::warn(
                "No smtphost or mailfrom available to contact users or admins, please check your system config");
        } else {
            std::string completeMail = generateErrorMail(mail_from, adminmails, subject);
            try {
                if (!mail::sendCurl(smtpUrl, mail_from, adminmails, completeMail)) {
                    spdlog::error("Failed to send email, please check the mailaddress in the DB Entry");
                }
            } catch (const std::exception& e) {
                spdlog::error("Exception while sending email: {}", e.what());
            }
        }
        return result;
    }

    spdlog::info("* CHECKING DB FOR WORKSPACES TO BE EXPIRED for filesystem: {}", fs);

    spdlog::info("  (keeptime: {} days, releasekeeptime: {} days)", config.getFsConfig(fs).keeptime,
                 config.getFsConfig(fs).releasekeeptime);

    // search expired active workspaces in DB
    for (auto const& id : db->matchPattern("*", "*", {}, false, false)) {
        result.active_seen++;
        std::unique_ptr<DBEntry> dbentry;
        // error logic first, we skip all loop body in case of bad entry
        try {
            dbentry = std::unique_ptr<DBEntry>(db->readEntry(id, false));
            if (!dbentry) {
                spdlog::error("skipping db entry {}", id);
                morbid_db_files.add(std::pair(id, fmt::format("could not read entry, filesystem: {}", fs)));
                continue;
            }
        } catch (DatabaseException& e) {
            spdlog::error(e.what());
            spdlog::error("skipping db entry {}", id);
            morbid_db_files.add(std::pair(id, fmt::format("database exeption, filesystem: {}", fs)));
            continue;
        }

        // entry is good

        auto expiration = dbentry->getExpiration();

        if (expiration <= 0) {
            spdlog::error("bad expiration in {}, skipping", id);
            morbid_db_files.add(std::pair(id, fmt::format("bad expiration, filesystem: {}", fs)));
            continue;
        }

        // do we have to expire?
        if (time((long*)0L) > expiration) {
            auto timestamp = to_string(time((long*)0L));

            result.active_expired++;
            if (!dryrun) {
                spdlog::info("  expiring {} (expired {})", id, utils::ctime(&expiration));
                // db entry first
                dbentry->expire(timestamp);

                // workspace second
                auto wspath = dbentry->getWSPath();
                try {
                    auto tgt = cppfs::path(wspath).remove_filename() / config.deletedPath(fs) /
                               (cppfs::path(wspath).filename().string() + "-" + timestamp);
                    if (debugflag) {
                        spdlog::debug("  mv ", wspath, " -> ", tgt.string());
                    }
                    robust_rename(wspath, tgt);
                } catch (cppfs::filesystem_error& e) {
                    spdlog::error("   failed to move workspace: {} ({})", wspath, e.what());
                }

                // send expiration mail, is opt-in from admin side per filesystem, and can be overridden by user
                std::string user_conf;
                if (config.getFsConfig(fs).expirationmail) {
                    auto wsuser = utils::getOwner(id);
                    if (debugflag) {
                        spdlog::debug("  sending expiration mail to {}", wsuser);
                    }
                    string user_conf_filename = user::getUserhome(wsuser) + "/.ws_user.conf";
                    if (!cppfs::is_symlink(user_conf_filename)) {
                        if (cppfs::is_regular_file(user_conf_filename)) {
                            user_conf = utils::getFileContents(user_conf_filename.c_str());
                        }
                        // FIXME: could be parsed here and passed as object not string
                    } else {
                        spdlog::error("~/.ws_user.conf can not be symlink!");
                        exit(-1);
                    }
                    UserConfig userconfig(user_conf);
                    if ((userconfig.getExpirationMail() && !dbentry->getMailaddress().empty()) ||
                        (dbentry->getReminder() > 0)) {
                        std::vector<std::string> mail_to;
                        mail_to.push_back(dbentry->getMailaddress());
                        std::string clustername = config.clustername();
                        int keeptime = config.getFsConfig(fs).keeptime;

                        std::string completeMail =
                            generateExpirationMail(mail_from, mail_to, expiration, id, fs, clustername, keeptime);
                        spdlog::info("    sending expiration mail to {} for entry {}", mail_to, id);

                        try {
                            if (!mail::sendCurl(smtpUrl, mail_from, mail_to, completeMail)) {
                                spdlog::error("Failed to send email, please check the mailaddress in the DB Entry");
                            }
                        } catch (const std::exception& e) {
                            spdlog::error("Exception while sending email: {}", e.what());
                        }
                    }
                }
            } else {
                spdlog::info("  would expire {} (expired {})", id, utils::ctime(&expiration));
            }
        } else {
            spdlog::info("   keeping (until {}, {} left): {}", utils::ctime(expiration),
                         formatTimedelta(expiration - time((long*)0L)), id);
            result.active_keep++;
            // Send reminder emails
            auto reminder = dbentry->getReminder();
            if (time((long*)0L) > (expiration - (reminder * (24 * 3600)))) {
                if (smtpUrl == "" || mail_from == "") {
                    spdlog::warn("No smtphost or mailfrom available to contact users, please check your system config");
                } else {
                    result.active_mails++;
                    if (dryrun) {
                        spdlog::info("    would send reminder mail to {} for entry {}", dbentry->getMailaddress(), id);
                    } else {
                        std::vector<std::string> mail_to;
                        mail_to.push_back(dbentry->getMailaddress());
                        std::string clustername = config.clustername();

                        std::string completeMail =
                            generateReminderMail(mail_from, mail_to, expiration, id, fs, clustername);
                        spdlog::info("    sending reminder mail to {} for entry {}", mail_to, id);
                        // fmt::print("{}", completeMail);
                        try {
                            if (!mail::sendCurl(smtpUrl, mail_from, mail_to, completeMail)) {
                                spdlog::error("Failed to send email, please check the mailaddress in the DB Entry");
                            }
                        } catch (const std::exception& e) {
                            spdlog::error("Exception while sending email: {}", e.what());
                        }
                    }
                }
            }
        }
    }

    spdlog::info(" =>  {} workspaces expired, {} kept.", result.active_expired, result.active_keep);
    spdlog::info("");
    spdlog::info("* CHECKING DELETED DB FOR WORKSPACES TO BE DELETED for filesystem: {}", fs);

    // search in DB for expired/released workspaces for those over keeptime to delete them
    for (auto const& id : db->matchPattern("*", "*", {}, true, false)) {
        result.inactive_seen++;
        std::unique_ptr<DBEntry> dbentry;
        try {
            dbentry = std::unique_ptr<DBEntry>(db->readEntry(id, true));
            if (!dbentry) {
                spdlog::error("skipping db entry {}", id);
                morbid_db_files.add(std::pair(id, fmt::format("could not read entry, filesystem: {}", fs)));
                continue;
            }
        } catch (DatabaseException& e) {
            spdlog::error(e.what());
            spdlog::error("skipping db entry {}", id);
            morbid_db_files.add(std::pair(id, fmt::format("database exeption, filesystem: {}", fs)));
            continue;
        }

        long releasetime;
        // we take the bigger one here, expired is 0 initialized and should remain 0 till expirer touches it
        auto expiration = std::max(dbentry->getExpiration(), dbentry->getExpired());
        auto keeptime = config.getFsConfig(fs).keeptime;

        // time to keep released workspaces
        long releasekeeptime;
        if (forcedeletereleased) {
            releasekeeptime = 0;
        } else {
            releasekeeptime = config.getFsConfig(fs).releasekeeptime;
        }

        std::string reason = "expired";

        // get released time from name = id
        try {
            releasetime = std::stol(utils::splitString(id, '-').at(
                std::count(id.begin(), id.end(), '-'))); // count from back, for usernames with "-"
        } catch (const out_of_range& e) {
            spdlog::error("skipping DB entry with unparsable name {}", id);
            morbid_db_files.add(std::pair(id, fmt::format("unparsable name, filesystem: {}", fs)));
            continue;
        } catch (const invalid_argument& e) {
            spdlog::error("skipping DB entry with unparsable name {}", id);
            morbid_db_files.add(std::pair(id, fmt::format("unparsable name, filesystem: {}", fs)));
            continue;
        }

        auto released = dbentry->getReleaseTime(); // check if it was released by user, 0 if not
        if (debugflag) {
            spdlog::debug("   released = {}, expiredtime (filename) = {}, expiration = {}", utils::ctime(released),
                          utils::ctime(releasetime), utils::ctime(expiration));
        }
        if (released > 1000000000L) { // released after 2001? if not ignore it
            releasetime = expiration = released;
            reason = "released";
        } else if (released != 0) {    // not released at all, expired, releasetime is taken from filename
            releasetime = 3000000000L; // date in future, 2065
            spdlog::warn("   IGNORING released {} for {}", releasetime, id);
        }

        // Only use releasekeeptime if workspace was actually released by user
        bool should_delete = false;
        if (released > 1000000000L) {
            // Released by user, use releasekeeptime
            should_delete = (time((long*)0L) >=
                             (releasetime + releasekeeptime * 24 * 3600)); // SPEC: releaskeeptime now also in days
        } else {
            // Expired, use keeptime
            // Compatability with v1 --> expired is 0 so expiration get chosen, but for v1 db entries we need the
            // releasetime from the filename
            if (dbentry->getExpired() == 0) {
                expiration = std::max(expiration, releasetime);
            }
            should_delete = (time((long*)0L) > (expiration + keeptime * 24 * 3600));
        }

        if (should_delete) {
            result.inactive_deleted++;
            spdlog::info("   {}delete DB entry {}, was {} {}", cleanermode ? "" : "would ", id, reason,
                         utils::ctime(&releasetime));

            if (cleanermode) {
                db->deleteEntry(id, true);
            }

            auto wspath = cppfs::path(dbentry->getWSPath()).remove_filename() / config.getFsConfig(fs).deletedPath / id;
            spdlog::info("    {}delete directory: {}", cleanermode ? "" : "would ", wspath.string());
            if (cleanermode) {
                try {
                    // timeout is now + deldirtimeout;
                    std::time_t deadline = std::time_t(std::time(nullptr)) + config.deldirtimeout();
                    spdlog::info("   deadline: {}/{} in {} seconds", deadline, utils::ctime(deadline),
                                 config.deldirtimeout());

                    utils::rmtree(wspath.string(), deadline);
                } catch (cppfs::filesystem_error& e) {
                    spdlog::error("  failed to remove: {} ({})", wspath.string(), e.what());
                }
            }
        } else {
            result.inactive_keep++;
            long wsdeadline;
            if (released > 1000000000L) {
                wsdeadline = releasetime + (releasekeeptime * 24 * 3600);
                spdlog::info("   keeping (until {}, {} left), was released: {}", utils::ctime(wsdeadline),
                             formatTimedelta(wsdeadline - time((long*)0L)), id);
            } else {
                wsdeadline = expiration + keeptime * 24 * 3600;
                spdlog::info("   keeping (until {}, {} left), was expired:  {}", utils::ctime(wsdeadline),
                             formatTimedelta(wsdeadline - time((long*)0L)), id);
            }
        }
    }
    spdlog::info(" =>  {} workspaces deleted, {} workspaces kept", result.inactive_deleted, result.inactive_keep);

    return result;
}

int main(int argc, char** argv) {

    // options and flags
    std::string filesystem;
    std::string single_space;
    std::string configfile;
    bool dryrun = true;
    bool summarymail = false;

    morbid_db_files_t morbid_db_files = {0, std::vector<std::pair<std::string, std::string>>()};

    po::variables_map opts;

    // locals settings to prevent strange effects
    utils::setCLocal();

    // initialize curl
    mail::initCurl();

    // set custom logging format, this different than other tools, as this tool is for root anyhow
    // setup minimal logging fist, config is not yet read for file logging
    setupMinimalLogging();

    // define options
    po::options_description cmd_options("\nOptions");
    // clang-format off
     cmd_options.add_options()
        ("help,h", "produce help message")
        ("version,V", "show version")
        ("filesystems,F", po::value<string>(&filesystem), "filesystems/workspaces to delete from, comma separated")
        ("space,s", po::value<string>(&single_space), "path of a single space that should be deleted")
        ("cleaner,c", "no dry-run mode")
        ("summary-mail,M", "send summary mail to admin after run")
        ("config", po::value<string>(&configfile), "path to configfile");
    // clang-format on

    po::options_description secret_options("Secret");
    // clang-format off
    secret_options.add_options()
        ("debug", "show debugging information")
        ("trace", "show tracing information")
        ("forcedeletereleased", "option for testing");
    // clang-format on

    po::options_description all_options;
    all_options.add(cmd_options).add(secret_options);

    // parse commandline
    try {
        po::store(po::command_line_parser(argc, argv).options(all_options).run(), opts);
        po::notify(opts);
    } catch (...) {
        fmt::println(stderr, "Usage: {} [options]\n", argv[0]);
        fmt::println(stderr, "{}", cmd_options);
        exit(1);
    }

    // global flags
    debugflag = opts.count("debug");
    traceflag = opts.count("trace");

    // handle options exiting here

    if (opts.count("help")) {
        fmt::println(stderr, "Usage: {} [options]\n", argv[0]);
        fmt::println(stderr, "{}", cmd_options);
        exit(0);
    }

    if (opts.count("version")) {
        utils::printVersion("ws_expirer");
        utils::printBuildFlags();
        exit(0);
    }

    if (opts.count("cleaner")) {
        cleanermode = true;
        dryrun = false;
    }

    if (opts.count("summary-mail")) {
        summarymail = true;
    }

    if (opts.count("forcedeletereleased")) {
        forcedeletereleased = true;
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

    spdlog::info("deldirtimeout = {} seconds", config.deldirtimeout());

    // now we can add file logging
    setupLogging(config.expirerlogpath());

    // main logic from here
    std::vector<std::string> fslist;

    if (opts.count("filesystems")) {
        // use only valid filesystems from commmand line
        for (auto const& fs : utils::splitString(filesystem, ',')) {
            if (canFind(config.Filesystems(), fs))
                fslist.push_back(fs);
        }
    } else {
        fslist = config.Filesystems();
    }

    if (debugflag) {
        spdlog::debug("fslist: {}", fslist);
    }

    spdlog::info("==== WS_EXPIRER {}RUN START {} =====", dryrun ? "DRY" : "", utils::ctime(std::time(nullptr)));

    if (cleanermode) {
        spdlog::warn("ws_expirer {} - REALLY CLEANING!", utils::getVersion());
    } else {
        spdlog::info("ws_expirer {} - SIMULATING CLEANING - DRYRUN", utils::getVersion());
    }

    // go through filesystem and
    // - delete stray directories first (directories with no DB entry)
    // - delete deleted ones not in DB
    // this searches over filesystem and checks DB
    std::vector<std::pair<std::string, clean_stray_result_t>> stray_stats;
    clean_stray_result_t total_stray = {0, 0, 0, 0};
    clean_stray_result_t fs_stray;
    for (auto const& fs : fslist) {
        fs_stray = clean_stray_directories(config, fs, single_space, dryrun);
        stray_stats.emplace_back(fs, fs_stray);
        total_stray += fs_stray;
    }
    spdlog::info(" Stray removal summary: {} valid, {} invalid, {} valid deleted, {} invalid deleted",
                 total_stray.valid_ws, total_stray.invalid_ws, total_stray.valid_deleted, total_stray.invalid_deleted);
    spdlog::info(" End of stray removal");
    spdlog::info("");

    // go through database and
    // - expire workspaces beyond expiration age and
    // - delete expired ones which are beyond keep date
    std::vector<std::pair<std::string, expire_result_t>> expire_stats;
    expire_result_t total_expire = {0, 0, 0, 0, 0, 0, 0};
    expire_result_t fs_expire;
    for (auto const& fs : fslist) {
        fs_expire = expire_workspaces(config, fs, dryrun, morbid_db_files);
        expire_stats.emplace_back(fs, fs_expire);
        total_expire += fs_expire;
    }
    spdlog::info(" Expiration summary: {} active seen, {} active keep, {} active expired, {} reminders sent, {} "
                 "inactive seen, {} inactive keep, {} inactive deleted",
                 total_expire.active_seen, total_expire.active_keep, total_expire.active_expired,
                 total_expire.active_mails, total_expire.inactive_seen, total_expire.inactive_keep,
                 total_expire.inactive_deleted);
    spdlog::info(" End of expiration");

    // Build summary string for logging and mail body
    std::string summary;
    auto append = [&](const std::string& line) {
        spdlog::info("{}", line);
        summary += line + "\n";
    };

    append(fmt::format("Dryrun: {}", dryrun));
    append("");
    append("Stray Summary");
    append("");
    append(fmt::format("  {:<15} {:>22} {:>16} {:>24}", "Filesystem", "Active [Valid Invalid]", "",
                       "Inactive [Valid Invalid]"));
    append(fmt::format("  {:->84}", ""));
    for (const auto& [fs, result] : stray_stats) {
        append(fmt::format("  {:<15} {:>7} {:>5} {:>7} {:>27} {:>5} {:>7}", fs, "", result.valid_ws, result.invalid_ws,
                           "", result.valid_deleted, result.invalid_deleted));
    }
    append(fmt::format("  {:->84}", ""));
    append(fmt::format("  {:<15} {:>7} {:>5} {:>7} {:>27} {:>5} {:>7}", "total", "", total_stray.valid_ws,
                       total_stray.invalid_ws, "", total_stray.valid_deleted, total_stray.invalid_deleted));
    append("");
    append("Expiration Summary");
    append("");
    append(fmt::format("  {:<15} {:>30} {:>4} {:>30}", "Filesystem", "Active [Seen Keep Expired Mails]", "",
                       "Inactive [Seen Keep Removed]"));
    append(fmt::format("  {:->84}", ""));
    for (const auto& [fs, result] : expire_stats) {
        append(fmt::format("  {:<15} {:>7} {:>4} {:>4} {:>7} {:>5} {:>17} {:>4} {:>4} {:>7}", fs, "",
                           result.active_seen, result.active_keep, result.active_expired, result.active_mails, "",
                           result.inactive_seen, result.inactive_keep, result.inactive_deleted));
    }
    append(fmt::format("  {:->84}", ""));
    append(fmt::format("  {:<15} {:>7} {:>4} {:>4} {:>7} {:>5} {:>17} {:>4} {:>4} {:>7}", "total", "",
                       total_expire.active_seen, total_expire.active_keep, total_expire.active_expired,
                       total_expire.active_mails, "", total_expire.inactive_seen, total_expire.inactive_keep,
                       total_expire.inactive_deleted));
    if (morbid_db_files.count != 0) {
        append("");
        append(" Morbid DB Files");
        for (const auto& [id, reason] : morbid_db_files.idreason) {
            append(fmt::format("   ID: {}, reason: {}", id, reason));
        }
        append("");
    }
    std::string runinfo =
        fmt::format("==== WS_EXPIRER {}RUN END {} =====", dryrun ? "DRY" : "", utils::ctime(std::time(nullptr)));
    append(runinfo);

    if (summarymail) {
        std::string smtpUrl = "smtp://" + config.smtphost();
        std::string mail_from = config.mailfrom();
        std::vector<std::string> adminmails = config.adminmail();
        std::string completeMail = generateSummaryMail(mail_from, adminmails, runinfo, summary);
        try {
            if (!mail::sendCurl(smtpUrl, mail_from, adminmails, completeMail)) {
                spdlog::error("Failed to send email, please check the mailaddress in the DB Entry");
            }
        } catch (const std::exception& e) {
            spdlog::error("Exception while sending email: {}", e.what());
        }
    }

    // Cleanup curl
    mail::cleanupCurl();

    return 0;
}
