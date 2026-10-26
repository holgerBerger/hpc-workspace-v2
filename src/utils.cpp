/*
 *  hpc-workspace-v2
 *
 *  utils.cpp
 *
 *  - helper functions
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

#include <cassert>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <sys/wait.h>
#include <vector>

#include "user.h"
#include "ws.h"

/*
#ifdef TERMCAP
#include <termcap.h>
#else
#include <term.h>
#endif
*/

namespace fs = std::filesystem;

#include "fmt/base.h"
#include "fmt/ranges.h" // IWYU pragma: keep

#include <grp.h>
#include <pwd.h>
#include <sys/types.h>
#include <unistd.h>

#include "caps.h"
#include "db.h"
#include "utils.h"

#include "spdlog/sinks/stdout_color_sinks.h"
#include "spdlog/sinks/syslog_sink.h"
#include "spdlog/spdlog.h"

using namespace std;

// globals
extern bool debugflag;
extern bool traceflag;
extern int debuglevel;
extern Cap caps;

// fwd
static std::size_t rmtree_fd(int topfd, std::string path, const std::time_t deadline);

namespace utils {

// fwd
static bool glob_match(char const* pat, char const* str);

// read a (small) file into a string
std::string getFileContents(const char* filename) {
    std::ifstream in(filename, std::ios::in | std::ios::binary);
    if (!in) {
        throw DatabaseException(fmt::format("could not open file {}", filename));
    }
    std::ostringstream contents;
    contents << in.rdbuf();
    return contents.str();
}

// read a (small) user-controlled file into a string, safely:
//  - does not follow a symlink at the last path component (O_NOFOLLOW)
//  - refuses anything but regular files (checked after open, no TOCTOU)
//  - O_NONBLOCK so a fifo can never hang the reader (typical DoS vs. a privileged process)
//  - bounds the read to maxSize bytes
// throws DatabaseException on any problem
std::string readFileNoFollow(const std::string& filename, std::size_t maxSize) {
    int fd = ::open(filename.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK);
    if (fd == -1) {
        throw DatabaseException(fmt::format("could not open file {} ({})", filename, strerror(errno)));
    }

    struct stat st;
    if (fstat(fd, &st) != 0) {
        int e = errno;
        close(fd);
        throw DatabaseException(fmt::format("could not stat file {} ({})", filename, strerror(e)));
    }
    if (!S_ISREG(st.st_mode)) {
        close(fd);
        throw DatabaseException(fmt::format("{} is not a regular file", filename));
    }
    if (static_cast<size_t>(st.st_size) > maxSize) {
        close(fd);
        throw DatabaseException(fmt::format("file {} is larger than {} bytes", filename, maxSize));
    }

    std::string content;
    char buf[65536];
    while (true) {
        ssize_t n = ::read(fd, buf, sizeof(buf));
        if (n == 0) {
            break;
        }
        if (n < 0) {
            int e = errno;
            if (e == EINTR) {
                continue;
            }
            close(fd);
            throw DatabaseException(fmt::format("error reading {} ({})", filename, strerror(e)));
        }
        content.append(buf, static_cast<size_t>(n));
        if (content.size() > maxSize) {
            // file grew past the limit after our stat()
            close(fd);
            throw DatabaseException(fmt::format("file {} is larger than {} bytes", filename, maxSize));
        }
    }
    close(fd);
    return content;
}

// write a (small) string to a file
void writeFile(const std::string filename, const std::string content) {
    std::ofstream out(filename);
    if (out.is_open()) {
        out << content;
        out.close();
    } else {
        spdlog::error("Can not open file {}!", filename);
    }
}

// get file names matching glob pattern from path, ("/etc", "p*d") -> passwd, match dirs if dirs==true
std::vector<string> dirEntries(const string path, const string pattern, const bool dirs) {
    if (traceflag)
        spdlog::trace("dirEntries({},{})", path, pattern);
    vector<string> fl;
    if (!fs::is_directory(path)) {
        spdlog::error("Directory {} does not exist.", path);
        return fl;
    }
    for (const auto& entry : fs::directory_iterator(path)) {
        if (entry.is_regular_file() || entry.is_symlink() || (dirs && entry.is_directory()))
            if (glob_match(pattern.c_str(), entry.path().filename().string().c_str())) {
                fl.push_back(entry.path().filename().string());
            }
    }
    return fl;
}

// glob matching stolen from linux kernel, under MIT/GPL
//   https://github.com/torvalds/linux/blob/master/lib/glob.c
static bool glob_match(char const* pat, char const* str) {
    if (traceflag)
        spdlog::trace("glob_match({},{})", pat, str);

    /*
     * Backtrack to previous * on mismatch and retry starting one
     * character later in the string.  Because * matches all characters
     * (no exception for /), it can be easily proved that there's
     * never a need to backtrack multiple levels.
     */
    char const *back_pat = NULL, *back_str = NULL;

    /*
     * Loop over each token (character or class) in pat, matching
     * it against the remaining unmatched tail of str.  Return false
     * on mismatch, or true after matching the trailing nul bytes.
     */
    for (;;) {
        unsigned char c = *str++;
        unsigned char d = *pat++;

        switch (d) {
        case '?': /* Wildcard: anything but nul */
            if (c == '\0')
                return false;
            break;
        case '*':             /* Any-length wildcard */
            if (*pat == '\0') /* Optimize trailing * case */
                return true;
            back_pat = pat;
            back_str = --str; /* Allow zero-length match */
            break;
        case '[': {        /* Character class */
            if (c == '\0') /* No possible match */
                return false;
            bool match = false, inverted = (*pat == '!');
            char const* cclass = pat + inverted;
            unsigned char a = *cclass++;
            /*
             * Iterate over each span in the character class.
             * A span is either a single character a, or a
             * range a-b.  The first span may begin with ']'.
             */
            do {
                unsigned char b = a;
                if (a == '\0') /* Malformed */
                    goto literal;
                if (cclass[0] == '-' && cclass[1] != ']') {
                    b = cclass[1];
                    if (b == '\0')
                        goto literal;
                    cclass += 2;
                    /* Any special action if a > b? */
                }
                match |= (a <= c && c <= b);
            } while ((a = *cclass++) != ']');
            if (match == inverted)
                goto backtrack;
            pat = cclass;
        } break;
        case '\\':
            d = *pat++;
            [[fallthrough]];
        default: /* Literal character */
        literal:
            if (c == d) {
                if (d == '\0')
                    return true;
                break;
            }
        backtrack:
            if (c == '\0' || !back_pat)
                return false; /* No point continuing */
            /* Try again from last *, one character later in str. */
            pat = back_pat;
            str = ++back_str;
            break;
        }
    }
}

// we only support C locale, if the used local is not installed on the system
// ws_allocate fails, this should be called in all tools
//  problem showed with a remote SuSE machine with DE locale, coming through ssh
void setCLocal() {
    setenv("LANG", "C", 1);
    setenv("LC_CTYPE", "C", 1);
    setenv("LC_ALL", "C", 1);
    std::setlocale(LC_ALL, "C");
    std::locale::global(std::locale("C"));
    // boost::filesystem::path::imbue(std::locale()); // FIXME: what is this in cpp filesystem?
}

// validate email address
//  TODO: unitest?
bool isValidEmail(const std::string& email) {
    if (traceflag)
        spdlog::trace("isValidEmail({})", email);

    // Regular expression for basic email validation (improved)
    const std::regex email_regex(
        R"((?:[a-z0-9!#$%&'*+/=?^_`{|}~-]+(?:\.[a-z0-9!#$%&'*+/=?^_`{|}~-]+)*|"(?:[\x01-\x08\x0b\x0c\x0e-\x1f\x21\x23-\x5b\x5d-\x7f]|\\[\x01-\x09\x0b\x0c\x0e-\x7f])*")@(?:(?:[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?\.)+[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?|\[(?:(?:25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\.){3}(?:25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\]))",
        std::regex_constants::icase); // Case-insensitive

    const std::regex localemail_regex(
        R"((?:[a-z0-9!#$%&'*+/=?^_`{|}~-]+(?:\.[a-z0-9!#$%&'*+/=?^_`{|}~-]+)*|"(?:[\x01-\x08\x0b\x0c\x0e-\x1f\x21\x23-\x5b\x5d-\x7f]|\\[\x01-\x09\x0b\x0c\x0e-\x7f])*")@localhost)",
        std::regex_constants::icase); // Case-insensitive

    if (!std::regex_match(email, email_regex)) {
        if (!std::regex_match(email, localemail_regex)) {
            return false;
        }
    }

    // Additional checks (length limits, etc.)
    if (email.length() > 254) { // RFC 5321 maximum length
        return false;
    }

    // Check for consecutive dots or @ at the beginning/end
    if (email.find("..") != std::string::npos || email.front() == '@' || email.back() == '@' ||
        email.find(".@") != std::string::npos || email.find("@.") != std::string::npos) {
        return false;
    }

    return true;
}

// get first line of a multiline string
std::string getFirstLine(const std::string& multilineString) {
    size_t pos = multilineString.find('\n');
    if (pos == std::string::npos) {
        // No newline found, return the entire string
        return multilineString;
    } else {
        // Extract the first line
        return multilineString.substr(0, pos);
    }
}

// getID returns id part of workspace id <username-id>
// can handle usernames and workspace IDs with "-" in it
// cost is high if > 1 dash in wsid, as getOwner checks vs existing usernames
std::string getID(const std::string wsid) {
    // count number of dashes in wsid
    size_t num_dashes = std::count(wsid.begin(), wsid.end(), '-');

    if (num_dashes == 1) {
        // happy case: wsid is "username-id"
        size_t last_dash = wsid.find_last_of('-');
        if (last_dash == std::string::npos) {
            return "";
        }
        return wsid.substr(last_dash + 1);
    } else {
        auto owner = getOwner(wsid);
        return wsid.substr(owner.length() + 1);
    }
}

// getOwner returns the owner part of workspace id <username-id>
// Updated to handle wsid format "username-id" where username can contain '-'
// This can also handle id containing '-' by testing possible usernames if they exist.
// If none exists, take the shortest one containing no '-'
// FIXME: this is expensive, as it calls user::exists a lot
std::string getOwner(const std::string& wsid) {
    size_t pos = wsid.find('-');
    if (pos == std::string::npos) {
        return "";
    }

    // Attempt to find the longest substring that corresponds to a real system user
    // starting from the last dash and moving backwards.
    size_t current_dash = wsid.find_last_of('-');
    while (current_dash != std::string::npos) {
        std::string candidate = wsid.substr(0, current_dash);
        if (user::exists(candidate)) {
            return candidate;
        }
        if (current_dash == 0)
            break;
        current_dash = wsid.find_last_of('-', current_dash - 1);
    }

    // If no existing user found, fallback to the shortest part before the first '-'
    return wsid.substr(0, pos);
}

/*
// print a character r times
static void repstr(char *c, int r) {
                for(int i=0; i<r; i++) {
                                cout << c;
                }
}

// check if person behind tty is human
bool ruh() {
srand (time(NULL));
int syllables = 3 + rand() % 5;

        // vector of japanese syllables
        // vector<string> syllable = {
        const char * syllable[] = {
                        "a","i","u","e","o",
                        "ka","ki","ku","ke","ko",
                        "sa","shi","su","se","so",
                        "ta","chi","tsu","te","to",
                        "na","ni","nu","ne","no",
                        "ha","hi","fu","he","ho",
                        "ma","mi","mu","me","mo",
                        "ya","yu","yo",
                        "ra","ri","ru","re","ro",
                        "wa","wi","we","wo","n",
                        NULL
        };

        const int syllablesize = 48;


vector<string> word;
string word_as_string;

for(int i=0; i<syllables; i++) {
        int r = rand() % syllablesize;
        word.push_back(syllable[r]);
        word_as_string.append(syllable[r]);
}

#ifdef TERMCAP
int r = tgetent(NULL,getenv("TERM"));
if (r != 1 ) {
        cerr << "unsupported terminal!" << endl;
        return false;
}
char *left  = tgetstr((char*)"le",NULL);
char *right = tgetstr((char*)"nd",NULL);
#else
        setupterm(NULL,1,NULL);
        char *left  = tigetstr("cub1");
        char *right = tigetstr("cuf1");
#endif

cout << "to verify that you are human, please type '";

if (rand() & 1) {
        // scheme 1
        cout << word[0];
        repstr(right, word[1].size());
        cout << word[2];
        repstr(left, word[1].size()+word[2].size());
        cout << word[1];
        repstr(right, word[2].size());
        for(unsigned int i=3; i<word.size(); i++) {
                cout << word[i];
        }
} else {
        // scheme 2
        repstr(right, word[0].size());
        cout << word[1];
        repstr(left, word[0].size()+word[1].size());
        cout << word[0];
        repstr(right, word[1].size());
        for(unsigned int i=2; i<word.size(); i++) {
                cout << word[i];
        }
}
cout << "': " << flush;

string line;
getline(cin, line);

if(line==word_as_string) {
        cout << "you are human\n";
        return true;
} else {
        cout << "not sure if you are human\n";
        return false;
}

}
*/

// aRe yoU Human?
// new version, no terminfo
bool new_ruh() {
    // clang-format off
    const std::map<std::string, std::vector<std::string>> wordmap = {
        {"0", {"Dog",   "Cat",  "Elephant", "Lion",  "Tiger",   "Cow",    "Horse",    "Monkey", "Snake",
               "Eagle", "Bear", "Wolf",     "Fox",   "Deer",    "Rabbit", "Squirrel", "Pig",    "Chicken",
               "Duck",  "Fish", "Shark",    "Whale", "Dolphin", "Frog",   "Butterfly"}},
        {"1", {"Apple",       "Banana", "Orange",    "Strawberry", "Blueberry",   "Watermelon", "Mango",
               "Pineapple",   "Grapes", "Avocado",   "Tomato",     "Potato",      "Carrot",     "Broccoli",
               "Spinach",     "Onion",  "Garlic",    "Cucumber",   "Bell pepper", "Lettuce",    "Corn",
               "Green beans", "Peas",   "Asparagus", "Zucchini"}},
        {"2", {"Car",   "House", "Tree",  "Book",       "Computer", "Phone", "Chair", "Table",   "Shirt",
               "Pants", "Shoes", "Hat",   "Door",       "Window",   "Wall",  "Floor", "Ceiling", "Lightbulb",
               "Pen",   "Paper", "Clock", "Television", "Radio",    "Cloud", "Rock"}},
        {"3", {"Albert Einstein", "Isaac Newton", "Leonardo da Vinci", "Mahatma Gandhi",
               "Nelson Mandela", "Martin Luther King Jr.", "Cleopatra", "Julius Caesar",
               "Alexander the Great", "Napoleon Bonaparte", "George Washington",
               "Abraham Lincoln", "Winston Churchill", "Marie Curie", "Rosalind Franklin",
               "Galileo Galilei", "Charles Darwin", "Sigmund Freud", "Siddhartha Gautama",
               "Confucius", "Socrates", "Plato", "Aristotle", "Homer"}},
    };
    // clang-format on

    srand(time(NULL));

    int cat_index = rand() % 3;
    int obj_index = rand() % 24;

    const std::vector<std::string> idx = {"0", "1", "2", "3"};

    auto ol = wordmap.at(idx[cat_index]);

    std::cout
        << fmt::format(
               "Is '{}' an [0] Animal, [1] Fruit or Vegetable or [2] Object or [3] Person? <type 0/1/2/3 + enter> :",
               ol[obj_index])
        << std::flush;

    string line;
    getline(cin, line);
    if (line.length() > 0) {
        if (line[0] - '0' == cat_index)
            return true;
    }

    return false;
}

// split a string at delimiter and return a vector of tokens
std::vector<std::string> splitString(const std::string& str, char delimiter) {
    std::vector<std::string> words;
    std::stringstream ss(str);
    std::string word;

    while (std::getline(ss, word, delimiter)) {
        words.push_back(word);
    }
    return words;
}

std::optional<time_t> timestampFromId(const std::string& id) {
    // same shape rules as splitExpiredId() in dbv1.cpp: last dash-separated field, non-empty
    // base and suffix, digits only
    auto dash = id.rfind('-');
    if (dash == std::string::npos || dash == 0 || dash + 1 >= id.size())
        return std::nullopt;
    const auto suffix = id.substr(dash + 1);
    if (!std::all_of(suffix.begin(), suffix.end(), [](unsigned char c) { return std::isdigit(c); }))
        return std::nullopt;
    try {
        return static_cast<time_t>(std::stoll(suffix));
    } catch (const std::exception&) { // out of range
        return std::nullopt;
    }
}

// parse ACL list into a config-ordered vector of (id, (modifier, intents)) entries
// an empty intent list should be interpreted as all permissions granted
auto parseACL(const std::vector<std::string> acl)
    -> std::vector<std::pair<std::string, std::pair<std::string, std::vector<int>>>> {
    if (traceflag)
        spdlog::trace("parseACL({})", acl);
    std::vector<std::pair<std::string, std::pair<std::string, std::vector<int>>>> entries;
    const std::regex acl_regex(R"(^([-+])?([^:]+)(:(\w+(,\w+)*)?)?$)");
    std::smatch base_match;
    for (const auto& entry : acl) {
        std::string modifier = "+";
        if (!std::regex_match(entry, base_match, acl_regex)) {
            // access control stays enabled (hasAccess gates on the raw list size),
            // but this entry grants/denies nothing - make it visible, it used to vanish silently
            spdlog::error("invalid ACL entry <{}>, ignoring", entry);
            continue;
        }
        if (base_match[1] == "+" || base_match[1] == "-") {
            modifier = base_match[1];
        }
        std::string id = base_match[2].str();
        std::string permissions = base_match[4].str();

        // split intents and convert strings to enum
        const auto tokens = splitString(permissions, ',');
        std::vector<int> numerical_intents;
        for (const auto& p : tokens) {
            if (ws::intentnames.count(p) > 0) {
                numerical_intents.push_back(ws::intentnames.at(p));
            } else {
                spdlog::error("invalid permission <{}> in ACL <{}>, ignoring", p, entry);
            }
        }
        // a given permission list where no name is valid must not silently degrade to
        // "all intents" (that would upgrade +bob:fin to a full grant); "bob" and "bob:"
        // without any name keep the documented empty-list meaning
        if (!tokens.empty() && numerical_intents.empty()) {
            spdlog::error("ACL entry <{}> has no valid permissions, ignoring", entry);
            continue;
        }
        entries.emplace_back(id, std::pair{modifier, std::move(numerical_intents)});
    }
    return entries;
}

// delete path be deleting contents and deleting path itself
// returns the number of files deleted
std::size_t rmtree(std::string path, std::time_t deadline) {
    if (traceflag) {
        spdlog::trace("rmtree({}, {})", path, deadline);
    }

    std::size_t deleted = 0;

    struct stat orig_stat, new_stat;

    int r = fstatat(AT_FDCWD, path.c_str(), &orig_stat, AT_SYMLINK_NOFOLLOW);
    if (r) {
        spdlog::error("fstatat {} -> {}", path, errno);
        return 0;
    }

    if (S_ISDIR(orig_stat.st_mode)) {
        int dirfd = openat(AT_FDCWD, path.c_str(), O_RDONLY | O_CLOEXEC);
        if (dirfd == -1) {
            spdlog::error("openat {} -> {}", path, errno);
            return 0;
        }
        r = fstatat(dirfd, "", &new_stat, AT_EMPTY_PATH);
        if (r == 0 && memcmp(&new_stat, &orig_stat, sizeof(struct stat)) == 0) {
            // rmtree_fd() takes ownership of dirfd and closes it
            deleted = rmtree_fd(dirfd, path, deadline);

            r = unlinkat(AT_FDCWD, path.c_str(), AT_REMOVEDIR);
            if (r) {
                spdlog::error("unlinkat {} -> {}", path, strerror(errno));
            }
        } else {
            close(dirfd);
        }
    }
    return deleted;
}

// delete path be deleting contents and deleting path itself
// without deadline
// returns the number of files deleted
std::size_t rmtree(std::string path) { return rmtree(path, (std::time_t)0L); }

// delete path be deleting contents and NOT deleting path itself
// with deadline
// returns the number of files deleted
std::size_t rmtree_below(std::string path) {
    auto deadline = (std::time_t)0L;

    if (traceflag) {
        spdlog::trace("rmtree_below({}, {})", path, deadline);
    }

    std::size_t deleted = 0;

    struct stat orig_stat, new_stat;

    int r = fstatat(AT_FDCWD, path.c_str(), &orig_stat, AT_SYMLINK_NOFOLLOW);
    if (r) {
        spdlog::error("fstatat {} -> {}", path, errno);
        return 0;
    }

    if (S_ISDIR(orig_stat.st_mode)) {
        int dirfd = openat(AT_FDCWD, path.c_str(), O_RDONLY | O_CLOEXEC);
        if (dirfd == -1) {
            spdlog::error("openat {} -> {}", path, errno);
            return 0;
        }
        r = fstatat(dirfd, "", &new_stat, AT_EMPTY_PATH);
        if (r == 0 && memcmp(&new_stat, &orig_stat, sizeof(struct stat)) == 0) {
            // rmtree_fd() takes ownership of dirfd and closes it
            deleted = rmtree_fd(dirfd, path, deadline);
        } else {
            close(dirfd);
        }
    }
    return deleted;
}

// pretty print a size in bytes
string prettyBytes(const uint64_t size) {
    string postfixes[] = {"B", "KB", "MB", "GB", "TB", "PB", "EP", "ZB", "YB", "RB", "QB"};
    double fsize = size;

    int index = 0;
    while (fsize >= 1000) {
        fsize /= 1000.0;
        index++;
    }

    return fmt::format("{:.3} {}", fsize, postfixes[index]);
}

// setup logging
//  set format
//  change to stderr
void setupLogging(const std::string ident) {
    // spdlog::set_pattern("%^%10l%$ : %v");

    if (user::isRoot()) {
        // stderr sink in color and with >= trace
        auto stderr_sink = std::make_shared<spdlog::sinks::stderr_color_sink_mt>();
        stderr_sink->set_pattern("%^%l%$: %v");

        auto stderr_logger = std::make_shared<spdlog::logger>("stderr_logger", stderr_sink);
        spdlog::set_default_logger(stderr_logger);
        spdlog::set_level(spdlog::level::trace);
    } else {
        // stderr sink in color and with >= info
        auto stderr_sink = std::make_shared<spdlog::sinks::stderr_color_sink_mt>();
        stderr_sink->set_level(spdlog::level::info);
        stderr_sink->set_pattern("%^%l%$: %v");

        // syslog sink for all levels
        auto syslog_sink = std::make_shared<spdlog::sinks::syslog_sink_mt>(ident, LOG_PID, LOG_USER, true);
        syslog_sink->set_level(spdlog::level::trace);
        syslog_sink->set_pattern("%l: %v");

        spdlog::logger* log = new spdlog::logger("multi_sink", {stderr_sink, syslog_sink});
        spdlog::set_default_logger(std::shared_ptr<spdlog::logger>(log));
        spdlog::set_level(spdlog::level::trace);
    }

    auto ws_debug_level = std::getenv("WS_DEBUG_LEVEL");
    if (ws_debug_level) {
        try {
            debuglevel = std::stoi(ws_debug_level);
        } catch (std::invalid_argument& e) {
            spdlog::error("ignoring invalid debuglevel");
            debuglevel = 0;
        }
    }
}

// right trim whitespaces from a string
std::string trimright(const std::string& in) {
    std::string str(in);
    str.erase(str.find_last_not_of(" \n\r\t") + 1);
    return str;
}

// right trim whitespaces from a string
std::string trimright(const char* in) {
    std::string str(in);
    str.erase(str.find_last_not_of(" \n\r\t") + 1);
    return str;
}

// thread safe ctime implementation, use this where std::ctime was used
// please note: this does NOT append a \n!
std::string ctime(const time_t* timer) {
    char buffer[80];

    auto ret = std::strftime(buffer, sizeof(buffer), "%c", localtime(timer));
    if (ret == 0) {
        spdlog::warn("bad strftime call in utils::ctime");
    }
    return std::string(buffer);
}

// thread safe ctime implementation, use this where std::ctime was used
// please note: this does NOT append a \n!
std::string ctime(const time_t ctimer) {
    char buffer[80];

    time_t timer = ctimer;
    auto ret = std::strftime(buffer, sizeof(buffer), "%c", localtime(&timer));
    if (ret == 0) {
        spdlog::warn("bad strftime call in utils::ctime");
    }
    return std::string(buffer);
}

std::string permstring(const fs::perms p) {
    stringstream str;
    using std::filesystem::perms;
    auto show = [&](char op, perms perm) { str << (perms::none == (perm & p) ? '-' : op); };
    show('r', perms::owner_read);
    show('w', perms::owner_write);
    show('x', perms::owner_exec);
    show('r', perms::group_read);
    show('w', perms::group_write);
    show('x', perms::group_exec);
    show('r', perms::others_read);
    show('w', perms::others_write);
    show('x', perms::others_exec);
    string ret;
    str >> ret;
    return ret;
}

/*
 * fallback for rename in case of EXDEV
 * we do not use system() as we are in setuid
 * and it would fail, and it sucks anyhow
 *
 * Returns 0 only if /bin/mv exited normally with status 0. A failed fork/waitpid
 * or a child killed by a signal (e.g. OOM during a large cross-device copy) is
 * reported as non-zero and logged here, so callers can keep DB entry and data
 * directory consistent.
 * NOTE: even a 0 exit can theoretically leave the source behind (mv killed right
 * after the last unlink but before exiting); the expirer stray cleanup catches
 * such leftovers on the next run.
 */
int mv(const char* source, const char* target) {
    pid_t pid = fork();
    if (pid < 0) {
        spdlog::error("mv: fork failed: {} ({})", errno, strerror(errno));
        return -1;
    }
    if (pid == 0) {
        // mv may run as root on behalf of the caller (setuid build): give it a fixed environment
        // instead of the caller's one
        char* const envp[] = {const_cast<char*>("PATH=/usr/bin:/bin"), const_cast<char*>("LC_ALL=C"), nullptr};
        execle("/bin/mv", "mv", "--", source, target, nullptr, envp);
        // we only get here if execle fails, so log the error and exit
        spdlog::error("execle mv failed, errno: {} ({})", errno, strerror(errno));
        _exit(127); // shell convention: command could not be executed
    }
    int status = 0;
    if (waitpid(pid, &status, 0) != pid) {
        spdlog::error("mv: waitpid failed: {} ({})", errno, strerror(errno));
        return -1;
    }
    if (WIFSIGNALED(status)) {
        spdlog::error("mv: killed by signal {}", WTERMSIG(status));
        return -1;
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

// helper to get a file time as a long long integer, representing the number of seconds since Unix epoch
long long getFileTimeAsLong(const fs::path& p) {
    std::error_code ec;
    auto ftime = fs::last_write_time(p, ec);
    if (ec) {
        spdlog::info("Failed to get creation time for '{}': {} ({}) (probably old DB entry)", p.string(), ec.message(),
                     ec.value());
        return 0;
    }

    // Convert file_clock to system_clock to align with Unix epoch if needed
    auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        ftime - fs::file_time_type::clock::now() + std::chrono::system_clock::now());

    // Extract duration as a long long integer (e.g., seconds or milliseconds)
    auto duration = sctp.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::seconds>(duration).count();
}

} // end of namespace utils

// internal recursive functions, based on file handles
// delete a directory and its contents, should be temper safe, with a deadline (epoch time)
// after the deadline is passed, no new recursion will be started,
// so the deadline is not hard but soft and can be missed significantly
// deadline==0 disables the deadline
// returns the number of files deleted
// delete the contents of the directory referenced by topfd, and topfd itself.
// rmtree_fd() takes ownership of topfd: it is always closed before returning,
// and callers must not close it again.
static std::size_t rmtree_fd(int topfd, std::string path, const std::time_t deadline) {
    std::size_t deleted = 0;

    if (traceflag) {
        spdlog::trace("rmtree_fd({}, {}, {})", topfd, path, deadline);
    }

    // check for deadline
    if (deadline != 0 && std::time((long*)0L) >= deadline) {
        spdlog::info("rmtree_fd deadline passed, exiting.");
        if (debugflag) {
            spdlog::debug("deadline={} time={}", deadline, std::time((long*)0L));
        }
        close(topfd);
        return deleted;
    }

    auto dir = fdopendir(topfd);
    if (dir == nullptr) {
        spdlog::error("fdopendir {} -> {}", topfd, errno);
        close(topfd);
        return deleted;
    }

    // collect entry names by value: readdir() returns pointers into the DIR stream's
    // internal buffer, which is refilled when the stream advances past it (directories
    // larger than the read buffer), so dirent pointers must not be kept across
    // subsequent readdir() calls.
    std::vector<std::pair<std::string, unsigned char>> entries;
    while (true) {
        errno = 0;
        auto entry = readdir(dir);
        if (entry == nullptr) {
            if (errno != 0) {
                spdlog::error("errno={}", errno);
            }
            break;
        }
        entries.emplace_back(entry->d_name, entry->d_type);
    }

    for (const auto& ent : entries) {
        const char* name = ent.first.c_str();

        bool isdir = ent.second == DT_DIR;
        if (ent.second == DT_UNKNOWN) {
            // some filesystems (e.g. NFS) do not provide d_type, resolve it by stat
            struct stat st;
            isdir = (fstatat(topfd, name, &st, AT_SYMLINK_NOFOLLOW) == 0) && S_ISDIR(st.st_mode);
        }

        if (isdir) {
            // ignore . and ..
            if (strcmp(name, ".") && strcmp(name, "..")) {
                struct stat orig_stat, new_stat;

                int r = fstatat(topfd, name, &orig_stat, AT_SYMLINK_NOFOLLOW);
                if (r) {
                    spdlog::error("fstatat {} {}/{} -> {}", topfd, path, name, errno);
                    continue;
                }

                if (S_ISDIR(orig_stat.st_mode)) {
                    int dirfd = openat(topfd, name, O_RDONLY | O_CLOEXEC);
                    if (dirfd == -1) {
                        spdlog::error("openat {}/{} -> {}", path, name, errno);
                        continue;
                    }
                    r = fstatat(dirfd, "", &new_stat, AT_EMPTY_PATH);
                    if (r == 0 && memcmp(&new_stat, &orig_stat, sizeof(struct stat)) == 0) {
                        deleted += rmtree_fd(dirfd, fs::path(path) / name, deadline);

                        r = unlinkat(topfd, name, AT_REMOVEDIR);
                        if (r) {
                            spdlog::error("unlinkat {}/{} -> {}", path, name, strerror(errno));
                        }
                    } else {
                        spdlog::error("rmtree hit a symbolic link!");
                        close(dirfd);
                    }
                }
            }
        } else {
            int r = unlinkat(topfd, name, 0);
            if (r) {
                spdlog::error("unlinkat {}/{} -> {}", path, name, strerror(errno));
            } else {
                deleted++;
            }
        }
    }
    // closes topfd
    closedir(dir);
    return deleted;
}
