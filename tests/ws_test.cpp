#include <catch2/catch_test_macros.hpp>
#include <string>

#include "../src/ws.h"

bool debugflag = false;
bool traceflag = false;
int debuglevel = 0;

TEST_CASE("ws::isValidWorkspaceName", "[ws]") {
    SECTION("valid names") {
        REQUIRE(ws::isValidWorkspaceName("a"));
        REQUIRE(ws::isValidWorkspaceName("9"));
        REQUIRE(ws::isValidWorkspaceName("myws"));
        REQUIRE(ws::isValidWorkspaceName("My_WS-1.0"));
        REQUIRE(ws::isValidWorkspaceName("a.."));
        REQUIRE(ws::isValidWorkspaceName("user-name-1700000000"));
    }

    SECTION("empty name") {
        REQUIRE_FALSE(ws::isValidWorkspaceName(""));
    }

    SECTION("invalid first character") {
        REQUIRE_FALSE(ws::isValidWorkspaceName("."));
        REQUIRE_FALSE(ws::isValidWorkspaceName(".."));
        REQUIRE_FALSE(ws::isValidWorkspaceName(".ws"));
        REQUIRE_FALSE(ws::isValidWorkspaceName("-ws"));
        REQUIRE_FALSE(ws::isValidWorkspaceName("_ws"));
    }

    SECTION("invalid characters") {
        REQUIRE_FALSE(ws::isValidWorkspaceName("a/b"));
        REQUIRE_FALSE(ws::isValidWorkspaceName("a b"));
        REQUIRE_FALSE(ws::isValidWorkspaceName("a\nb"));
        REQUIRE_FALSE(ws::isValidWorkspaceName("a;rm"));
        REQUIRE_FALSE(ws::isValidWorkspaceName("a*"));
        REQUIRE_FALSE(ws::isValidWorkspaceName(std::string("a\0b", 3)));
        REQUIRE_FALSE(ws::isValidWorkspaceName("\xc3\xa4"));  // UTF-8 umlaut
        REQUIRE_FALSE(ws::isValidWorkspaceName("a\xc3\xa4")); // non-ASCII after valid start
    }

    SECTION("long names do not crash") {
        REQUIRE(ws::isValidWorkspaceName(std::string(1000000, 'a')));
        REQUIRE_FALSE(ws::isValidWorkspaceName(std::string(1000000, 'a') + "/"));
    }
}
