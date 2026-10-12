#include <filesystem>
#define CATCH_CONFIG_MAIN // This tells Catch to provide a main() - only do this in one cpp file
#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

#include "../src/mail.h"
#include "../src/utils.h"
#include "../src/ws.h"

namespace fs = std::filesystem;

bool debugflag = false;
bool traceflag = false;
int debuglevel = 0;

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
