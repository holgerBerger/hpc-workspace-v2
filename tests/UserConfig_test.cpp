#define CATCH_CONFIG_MAIN // This tells Catch to provide a main() - only do this in one cpp file
#include <catch2/catch_test_macros.hpp>

#include "../src/UserConfig.h"

bool debugflag = false;
bool traceflag = false;

TEST_CASE("User config yaml format", "[userconfig]")
{

    SECTION("user config reads all fields")
    {
        std::string userconf =
            R"yaml(
mail: address@mail.com
reminder: 1
groupname: beatles
duration: 10
)yaml";

        UserConfig uc(userconf);

        REQUIRE(uc.getMailaddress() == "address@mail.com");
        REQUIRE(uc.getGroupname() == "beatles");
        REQUIRE(uc.getDuration() == 10);
        REQUIRE(uc.getReminder() == 1);
    }

    SECTION("user config check defaults for empty config")
    {
        std::string userconf =
            R"yaml(
)yaml";

        UserConfig uc(userconf);

        REQUIRE(uc.getMailaddress() == "");
        REQUIRE(uc.getGroupname() == "");
        REQUIRE(uc.getDuration() == -1);
        REQUIRE(uc.getReminder() == -1);
    }

    SECTION("user config ignores invalid email")
    {

        std::string userconf =
            R"yaml(
mail: 1-invalid_mail_address!
)yaml";

        UserConfig uc(userconf);

        REQUIRE(uc.getMailaddress() == "");
    }
}

TEST_CASE("User config old format", "[userconfig]")
{

    SECTION("user config reads single line valid email")
    {
        std::string userconf =
            R"(address@mail.com
)";

        UserConfig uc(userconf);

        REQUIRE(uc.getMailaddress() == "address@mail.com");
    }

    SECTION("user config ignores single line invalid email")
    {
        std::string userconf =
            R"(1-invalid_mail_address!
)";

        UserConfig uc(userconf);

        REQUIRE(uc.getMailaddress() == "");
    }
}
