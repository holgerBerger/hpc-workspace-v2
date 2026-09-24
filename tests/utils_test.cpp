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
        REQUIRE(m["a"].first == "-");
        REQUIRE(m["abc"].first == "+");
        REQUIRE(m["c"].first == "+");
        REQUIRE(m["y"].first == "-");
        REQUIRE(m["a"].second.size() == 0);
        REQUIRE(m["abc"].second.size() == 2);
        REQUIRE(m["c"].second.size() == 0);
        REQUIRE(m["abc"].second == std::vector<int>{ws::LIST, ws::CREATE});
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
