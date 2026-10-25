#include <filesystem>
#define CATCH_CONFIG_MAIN // This tells Catch to provide a main() - only do this in one cpp file
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <climits>
#include <string>
#include <vector>

#include <unistd.h>

#include "../src/caps.h"
#include "../src/mail.h"
#include "../src/utils.h"
#include "../src/ws.h"

namespace fs = std::filesystem;

bool debugflag = false;
bool traceflag = false;
int debuglevel = 0;

// init caps here, when euid!=uid
Cap caps{};

TEST_CASE("utils", "[utils]") {
    SECTION("parceACL") {
        auto m = utils::parseACL({"-a", "+abc:list,create", "-z", "c", "-y"});
        REQUIRE(m.size() == 5);
        // config order is kept
        REQUIRE(m[0].first == "a");
        REQUIRE(m[0].second.first == "-");
        REQUIRE(m[0].second.second.size() == 0);
        REQUIRE(m[1].first == "abc");
        REQUIRE(m[1].second.first == "+");
        REQUIRE(m[1].second.second == std::vector<int>{ws::LIST, ws::CREATE});
        REQUIRE(m[2].first == "z");
        REQUIRE(m[2].second.first == "-");
        REQUIRE(m[3].first == "c");
        REQUIRE(m[3].second.first == "+");
        REQUIRE(m[3].second.second.size() == 0);
        REQUIRE(m[4].first == "y");
        REQUIRE(m[4].second.first == "-");

        // syntactically invalid entries are dropped; an entry whose permission list contains only
        // invalid names is dropped as well (it must not silently degrade to "all intents"); a bare
        // name or a trailing colon keeps the documented empty-list meaning
        auto d = utils::parseACL({"bad:entry,", "typo:nonexist", "ok:list", "colon:", "mix:list,typo"});
        REQUIRE(d.size() == 3);
        REQUIRE(d[0].first == "ok");
        REQUIRE(d[0].second.second == std::vector<int>{ws::LIST});
        REQUIRE(d[1].first == "colon");
        REQUIRE(d[1].second.second.size() == 0);
        REQUIRE(d[2].first == "mix");
        REQUIRE(d[2].second.second == std::vector<int>{ws::LIST});

        // duplicate ids are kept as separate entries (hasAccess combines them as deny-wins)
        auto dup = utils::parseACL({"dup:list", "dup:create"});
        REQUIRE(dup.size() == 2);
        REQUIRE(dup[0].second.second == std::vector<int>{ws::LIST});
        REQUIRE(dup[1].second.second == std::vector<int>{ws::CREATE});
    }

    SECTION("rmtree") {
        fs::create_directories("/tmp/_wsTT/a/a/a/a");
        fs::create_directories("/tmp/_wsTT/a/a/a/b");
        REQUIRE(fs::exists("/tmp/_wsTT"));
        utils::writeFile("/tmp/_wsTT/file1", "file1");
        utils::writeFile("/tmp/_wsTT/a/file2", "file2");
        utils::writeFile("/tmp/_wsTT/a/a/a/file3", "file3");
        REQUIRE(utils::rmtree("/tmp/_wsTT") == 3);
        REQUIRE(fs::exists("/tmp/_wsTT") == false);
    }

    SECTION("readFileNoFollow") {
        auto dir = fs::temp_directory_path() / "_wsRNF";
        fs::remove_all(dir);
        fs::create_directories(dir);
        utils::writeFile((dir / "conf").string(), "mail: a@b.c\n");

        // regular file: returns content
        REQUIRE(utils::readFileNoFollow((dir / "conf").string()) == "mail: a@b.c\n");

        // symlink at last component must not be followed
        fs::create_symlink("conf", dir / "link");
        REQUIRE_THROWS_AS(utils::readFileNoFollow((dir / "link").string()), std::exception);

        // oversize file is rejected
        REQUIRE_THROWS_AS(utils::readFileNoFollow((dir / "conf").string(), 4), std::exception);

        // missing file throws, does not hang or crash
        REQUIRE_THROWS_AS(utils::readFileNoFollow((dir / "nothere").string()), std::exception);

        fs::remove_all(dir);
    }

    SECTION("prettySize") {
        REQUIRE(utils::prettyBytes(100) == "100 B");
        REQUIRE(utils::prettyBytes(1000) == "1 KB");
        REQUIRE(utils::prettyBytes(1500000) == "1.5 MB");
    }

    SECTION("trimright") {
        REQUIRE(utils::trimright("- \n") == std::string("-"));
        REQUIRE(utils::trimright(std::string("- \n")) == std::string("-"));
        REQUIRE(utils::trimright(" ") == "");
    }

    SECTION("ctime") {
        auto t = 1754337449L;
        REQUIRE(utils::ctime(&t) == std::string("Mon Aug  4 21:57:29 2025"));
    }

    SECTION("mail") {
        REQUIRE(mail::generateToHeader({"a"}) == std::string("a"));
        REQUIRE(mail::generateToHeader({"a", "b", "c"}) == std::string("a, b, c"));

        REQUIRE(mail::generateMessageID(".test") != mail::generateMessageID(".test"));
    }

    SECTION("Valid email addresses") {
        REQUIRE(utils::isValidEmail("test@example.com"));
        REQUIRE(utils::isValidEmail("user.name@example.org"));
        REQUIRE(utils::isValidEmail("user+tag@example.net"));
        REQUIRE(utils::isValidEmail("firstname.lastname@domain.co.uk"));
        REQUIRE(utils::isValidEmail("email@123.123.123.123"));
        REQUIRE(utils::isValidEmail("1234567890@example.com"));
        REQUIRE(utils::isValidEmail("email@example-one.com"));
        REQUIRE(utils::isValidEmail("_______@example.com"));
        REQUIRE(utils::isValidEmail("email@example.name"));
        REQUIRE(utils::isValidEmail("test.email.with+symbol@example.com"));
        REQUIRE(utils::isValidEmail("x@example.com"));
    }

    SECTION("Invalid email addresses - malformed") {
        REQUIRE_FALSE(utils::isValidEmail("plainaddress"));
        REQUIRE_FALSE(utils::isValidEmail("@missingusername.com"));
        REQUIRE_FALSE(utils::isValidEmail("username@.com"));
        REQUIRE_FALSE(utils::isValidEmail("username..double.dot@example.com"));
        REQUIRE_FALSE(utils::isValidEmail("username@com"));
        REQUIRE_FALSE(utils::isValidEmail("username@-example.com"));
        REQUIRE_FALSE(utils::isValidEmail("username@example-.com"));
        REQUIRE_FALSE(utils::isValidEmail(""));
    }

    SECTION("Invalid email addresses - consecutive dots and @ placement") {
        REQUIRE_FALSE(utils::isValidEmail("user..name@example.com"));
        REQUIRE_FALSE(utils::isValidEmail("user@example..com"));
        REQUIRE_FALSE(utils::isValidEmail("@example.com"));
        REQUIRE_FALSE(utils::isValidEmail("user@"));
        REQUIRE_FALSE(utils::isValidEmail("user.@example.com"));
        REQUIRE_FALSE(utils::isValidEmail("user@.example.com"));
    }

    SECTION("Invalid email addresses - too long") {
        std::string long_email = std::string(250, 'a') + "@example.com"; // 261 characters total
        REQUIRE_FALSE(utils::isValidEmail(long_email));
    }

    SECTION("Valid email addresses - edge cases") {
        REQUIRE(utils::isValidEmail("a@b.co"));
        REQUIRE(utils::isValidEmail("test@example.museum"));
        REQUIRE(utils::isValidEmail("user#test@example.com"));
        REQUIRE(utils::isValidEmail("user$test@example.com"));
        REQUIRE(utils::isValidEmail("user%test@example.com"));
        REQUIRE(utils::isValidEmail("user&test@example.com"));
        REQUIRE(utils::isValidEmail("user'test@example.com"));
        REQUIRE(utils::isValidEmail("user*test@example.com"));
        REQUIRE(utils::isValidEmail("user=test@example.com"));
        REQUIRE(utils::isValidEmail("user?test@example.com"));
        REQUIRE(utils::isValidEmail("user^test@example.com"));
        REQUIRE(utils::isValidEmail("user_test@example.com"));
        REQUIRE(utils::isValidEmail("user`test@example.com"));
        REQUIRE(utils::isValidEmail("user{test@example.com"));
        REQUIRE(utils::isValidEmail("user|test@example.com"));
        REQUIRE(utils::isValidEmail("user}test@example.com"));
        REQUIRE(utils::isValidEmail("user~test@example.com"));
    }
}

TEST_CASE("utils::mv", "[utils]") {
    namespace fs = std::filesystem;

    fs::path tmpbase = fs::temp_directory_path() / ("wsmvtest-" + std::to_string(getpid()));
    fs::remove_all(tmpbase);
    fs::create_directories(tmpbase);

    SECTION("successful move returns 0 and moves the file") {
        utils::writeFile((tmpbase / "src").string(), "content");
        REQUIRE(utils::mv((tmpbase / "src").c_str(), (tmpbase / "dst").c_str()) == 0);
        REQUIRE(fs::exists(tmpbase / "dst"));
        REQUIRE_FALSE(fs::exists(tmpbase / "src"));
    }

    SECTION("failing move does not report success") {
        // the regression: a mv that can not do its work must not return 0
        REQUIRE(utils::mv((tmpbase / "does-not-exist").c_str(), (tmpbase / "dst").c_str()) != 0);
        REQUIRE_FALSE(fs::exists(tmpbase / "dst"));
    }

    SECTION("operand names starting with a dash are not parsed as options") {
        // requires relative paths (as the dash would only reach mv as a leading char then),
        // and `--` as end-of-options marker in utils::mv
        utils::writeFile((tmpbase / "-v").string(), "content");
        auto savedir = fs::current_path();
        fs::current_path(tmpbase);
        int ret = utils::mv("-v", "moved");
        fs::current_path(savedir);
        REQUIRE(ret == 0);
        REQUIRE(fs::exists(tmpbase / "moved"));
        REQUIRE_FALSE(fs::exists(tmpbase / "-v"));
    }

    fs::remove_all(tmpbase);
}

TEST_CASE("utils::timestampFromId", "[utils]") {
    SECTION("well-formed deleted-area ID returns the timestamp") {
        auto ts = utils::timestampFromId("alice-proj-1699000000");
        REQUIRE(ts.has_value());
        REQUIRE(*ts == 1699000000);
    }

    SECTION("dashes in user and workspace name do not shift the suffix") {
        auto ts = utils::timestampFromId("al-ice-my-proj-1699000000");
        REQUIRE(ts.has_value());
        REQUIRE(*ts == 1699000000);
    }

    SECTION("leading zeros are accepted") {
        auto ts = utils::timestampFromId("alice-001699000000");
        REQUIRE(ts.has_value());
        REQUIRE(*ts == 1699000000);
    }

    SECTION("empty base before the dash is rejected") {
        // same shape rule as splitExpiredId() in dbv1.cpp
        REQUIRE_FALSE(utils::timestampFromId("-1699000000").has_value());
    }

    SECTION("non-numeric suffix returns nullopt") {
        // the regression: unguarded std::stol threw std::invalid_argument here
        REQUIRE_FALSE(utils::timestampFromId("alice-proj-backup").has_value());
    }

    SECTION("trailing dash returns nullopt") {
        // the regression: splitString drops the empty last field, so indexing with the dash
        // count threw std::out_of_range
        REQUIRE_FALSE(utils::timestampFromId("alice-proj-").has_value());
    }

    SECTION("missing dash or empty ID returns nullopt") {
        REQUIRE_FALSE(utils::timestampFromId("aliceproj").has_value());
        REQUIRE_FALSE(utils::timestampFromId("").has_value());
        REQUIRE_FALSE(utils::timestampFromId("-").has_value());
    }

    SECTION("suffix with sign or space is rejected") {
        REQUIRE_FALSE(utils::timestampFromId("alice-+123").has_value());
        REQUIRE_FALSE(utils::timestampFromId("alice- 123").has_value());
    }

    SECTION("digits beyond time_t range return nullopt instead of throwing") {
        REQUIRE_FALSE(utils::timestampFromId("alice-99999999999999999999999").has_value());
    }
}

TEST_CASE("mail::generateMailDateFormat", "[mail]") {
    // the tests pin the zone via TZ; restore whatever the process had afterwards
    const char* old_tz = getenv("TZ");
    const std::string saved_tz = old_tz ? old_tz : "";
    const bool had_tz = old_tz != nullptr;

    auto set_tz = [](const char* tz) {
        setenv("TZ", tz, 1);
        tzset();
    };

    SECTION("fixed UTC timestamps format as RFC 2822 with English names") {
        set_tz("UTC");
        REQUIRE(mail::generateMailDateFormat(0) == "Thu, 01 Jan 1970 00:00:00 +0000");
        REQUIRE(mail::generateMailDateFormat(1291038869) == "Mon, 29 Nov 2010 13:54:29 +0000");
    }

    SECTION("pre-1970 times work") {
        set_tz("UTC");
        REQUIRE(mail::generateMailDateFormat(-86400) == "Wed, 31 Dec 1969 00:00:00 +0000");
        set_tz("America/New_York");
        REQUIRE(mail::generateMailDateFormat(-86400) == "Tue, 30 Dec 1969 19:00:00 -0500");
    }

    SECTION("DST and half-hour offsets are taken from the local timezone") {
        set_tz("America/New_York");
        REQUIRE(mail::generateMailDateFormat(1609459200) == "Thu, 31 Dec 2020 19:00:00 -0500"); // EST
        REQUIRE(mail::generateMailDateFormat(1593561600) == "Tue, 30 Jun 2020 20:00:00 -0400"); // EDT
        set_tz("Asia/Kolkata");
        REQUIRE(mail::generateMailDateFormat(1793500000) == "Sun, 01 Nov 2026 07:56:40 +0530");
    }

    // regression: the old buffer was sized for exactly 31 output chars (year 9999 still fits),
    // longer output made strftime return 0 and string(timeString) read uninitialized stack
    SECTION("4-digit year, the longest output the old buffer held") {
        set_tz("UTC");
        REQUIRE(mail::generateMailDateFormat(253402300799) == "Fri, 31 Dec 9999 23:59:59 +0000");
    }

    SECTION("5-digit year, 32 chars") {
        set_tz("UTC");
        REQUIRE(mail::generateMailDateFormat(300000000000) == "Tue, 15 Aug 11476 05:20:00 +0000");
    }

    SECTION("7-digit year, 34 chars") {
        set_tz("UTC");
        REQUIRE(mail::generateMailDateFormat(100000000000000) == "Sat, 07 Nov 3170843 09:46:40 +0000");
    }

    SECTION("timestamps outside localtime_r range yield a placeholder, not garbage or a fake date") {
        set_tz("UTC");
        REQUIRE(mail::generateMailDateFormat(LLONG_MAX) == "(date out of range)");
        REQUIRE(mail::generateMailDateFormat(LLONG_MIN) == "(date out of range)");
    }

    if (had_tz)
        setenv("TZ", saved_tz.c_str(), 1);
    else
        unsetenv("TZ");
    tzset();
}

TEST_CASE("utils::getFirstLine", "[utils]") {
    SECTION("returns text before the first newline") { REQUIRE(utils::getFirstLine("a\nb\nc") == "a"); }
    SECTION("no newline returns whole string") { REQUIRE(utils::getFirstLine("single") == "single"); }
    SECTION("empty string") { REQUIRE(utils::getFirstLine("") == ""); }
    SECTION("leading newline yields empty first line") { REQUIRE(utils::getFirstLine("\nrest") == ""); }
    SECTION("trailing newline") { REQUIRE(utils::getFirstLine("a\n") == "a"); }
}

TEST_CASE("utils::splitString", "[utils]") {
    SECTION("splits on delimiter") {
        REQUIRE(utils::splitString("a,b,c", ',') == std::vector<std::string>{"a", "b", "c"});
    }
    SECTION("no delimiter is a single token") { REQUIRE(utils::splitString("abc", ',') == std::vector<std::string>{"abc"}); }
    SECTION("empty string yields no tokens") { REQUIRE(utils::splitString("", ',').empty()); }
    SECTION("consecutive delimiters yield empty tokens") {
        REQUIRE(utils::splitString("a,,b", ',') == std::vector<std::string>{"a", "", "b"});
    }
    SECTION("trailing delimiter does not add an empty token") {
        REQUIRE(utils::splitString("a,b,", ',') == std::vector<std::string>{"a", "b"});
    }
}

TEST_CASE("utils::permstring", "[utils]") {
    using fperms = std::filesystem::perms;
    REQUIRE(utils::permstring(fperms::all) == "rwxrwxrwx");
    REQUIRE(utils::permstring(fperms::none) == "---------");
    REQUIRE(utils::permstring(fperms::owner_all) == "rwx------");
    // 0644 -> rw-r--r--  ;  0755 -> rwxr-xr-x
    REQUIRE(utils::permstring(static_cast<fperms>(0644)) == "rw-r--r--");
    REQUIRE(utils::permstring(static_cast<fperms>(0755)) == "rwxr-xr-x");
}

TEST_CASE("utils::getFileContents", "[utils]") {
    auto dir = fs::temp_directory_path() / ("_wsGFC-" + std::to_string(getpid()));
    fs::remove_all(dir);
    fs::create_directories(dir);

    SECTION("round-trips written content") {
        utils::writeFile((dir / "f").string(), "hello world");
        REQUIRE(utils::getFileContents((dir / "f").string()) == "hello world");
    }

    SECTION("empty file reads as empty string") {
        utils::writeFile((dir / "empty").string(), "");
        REQUIRE(utils::getFileContents((dir / "empty").string()) == "");
    }

    SECTION("missing file throws") { REQUIRE_THROWS_AS(utils::getFileContents((dir / "nothere").string()), std::exception); }

    fs::remove_all(dir);
}

TEST_CASE("utils::dirEntries", "[utils]") {
    auto dir = fs::temp_directory_path() / ("_wsDE-" + std::to_string(getpid()));
    fs::remove_all(dir);
    fs::create_directories(dir / "sub");
    utils::writeFile((dir / "a.txt").string(), "x");
    utils::writeFile((dir / "b.txt").string(), "x");
    utils::writeFile((dir / "c.log").string(), "x");

    SECTION("glob filters by pattern") {
        auto txt = utils::dirEntries(dir.string(), "*.txt", false);
        std::sort(txt.begin(), txt.end());
        REQUIRE(txt == std::vector<std::string>{"a.txt", "b.txt"});
    }

    SECTION("dirs==false excludes directories") {
        auto files = utils::dirEntries(dir.string(), "*", false);
        std::sort(files.begin(), files.end());
        REQUIRE(files == std::vector<std::string>{"a.txt", "b.txt", "c.log"});
    }

    SECTION("dirs==true includes directories") {
        auto all = utils::dirEntries(dir.string(), "*", true);
        std::sort(all.begin(), all.end());
        REQUIRE(all == std::vector<std::string>{"a.txt", "b.txt", "c.log", "sub"});
    }

    SECTION("nonexistent directory returns empty") { REQUIRE(utils::dirEntries((dir / "gone").string(), "*", true).empty()); }

    fs::remove_all(dir);
}

TEST_CASE("utils::getFileTimeAsLong", "[utils]") {
    auto dir = fs::temp_directory_path() / ("_wsGFT-" + std::to_string(getpid()));
    fs::remove_all(dir);
    fs::create_directories(dir);
    utils::writeFile((dir / "f").string(), "x");

    SECTION("existing file has a positive timestamp") { REQUIRE(utils::getFileTimeAsLong(dir / "f") > 0); }

    SECTION("missing file returns 0") { REQUIRE(utils::getFileTimeAsLong(dir / "nothere") == 0); }

    fs::remove_all(dir);
}

TEST_CASE("utils::getDBYamlReader", "[utils]") {
    // compile-time constant; the build selects one of the two readers
    const std::string reader = utils::getDBYamlReader();
    REQUIRE((reader == "yaml-cpp" || reader == "RapidYAML"));
}

TEST_CASE("utils::SrcPos", "[utils]") {
    REQUIRE(utils::SrcPos("f.cpp", 42, "foo").getSrcPos() == "f.cpp:42[foo]");
}

TEST_CASE("utils::getOwner and getID", "[utils]") {
    SECTION("single dash uses the simple split") {
        REQUIRE(utils::getOwner("root-123") == "root");
        REQUIRE(utils::getID("root-123") == "123");
    }

    SECTION("multi-dash resolves the owner against existing users") {
        // "root" exists; "root-a" / "root-a-b" do not, so owner is "root" and id keeps the rest
        REQUIRE(utils::getOwner("root-a-b") == "root");
        REQUIRE(utils::getID("root-a-b") == "a-b");
    }

    SECTION("no dash yields empty owner") { REQUIRE(utils::getOwner("nodash").empty()); }
}

TEST_CASE("utils::HasGroupIntersection", "[utils]") {
    SECTION("known user shares groups with itself") {
        utils::HasGroupIntersection h("root");
        REQUIRE(h.hasCommonGroups("root"));
    }

    SECTION("unknown user is assumed to intersect") {
        utils::HasGroupIntersection h("root");
        REQUIRE(h.hasCommonGroups("this_user_does_not_exist_9f3a"));
    }

    SECTION("result is cached (same answer on repeat)") {
        utils::HasGroupIntersection h("root");
        REQUIRE(h.hasCommonGroups("daemon") == h.hasCommonGroups("daemon"));
    }
}
