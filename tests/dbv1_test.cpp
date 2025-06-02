#define CATCH_CONFIG_MAIN // This tells Catch to provide a main() - only do this in one cpp file
#include <catch2/catch_test_macros.hpp>

#include "../src/caps.h"
#include "../src/dbv1.h"

Cap caps{};

bool debugflag = false;
bool traceflag = false;

TEST_CASE("Database v1 yaml parser", "[dbv1]")
{

    DBEntryV1 entry(nullptr);

    entry.readFromString(std::string{
        R"yaml(
workspace: /a/path
expiration: 1734701876
extensions: 3
reminder: 0
mailaddress: ""
comment: ""
)yaml"});

    SECTION("readentry")
    {
        REQUIRE(entry.getWSPath() == "/a/path");
        REQUIRE(entry.getExpiration() == 1734701876);
        REQUIRE(entry.getExtension() == 3);
        REQUIRE(entry.getMailaddress() == "");
    }
}
