#define CATCH_CONFIG_MAIN // This tells Catch to provide a main() - only do this in one cpp file
#include <catch2/catch_test_macros.hpp>

#include "../src/UserConfig.h"

bool debugflag = false;
bool traceflag = false;
int debuglevel = 0;

TEST_CASE("User config yaml format", "[userconfig]") {

    SECTION("user config reads all fields") {
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

    SECTION("user config with comment") {
        std::string userconf =
            R"yaml(#comment test
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

    SECTION("user config check defaults for empty config") {
        std::string userconf =
            R"yaml(
)yaml";

        UserConfig uc(userconf);

        REQUIRE(uc.getMailaddress() == "");
        REQUIRE(uc.getGroupname() == "");
        REQUIRE(uc.getDuration() == -1);
        REQUIRE(uc.getReminder() == -1);
    }

    SECTION("user config ignores invalid email") {

        std::string userconf =
            R"yaml(
mail: 1-invalid_mail_address!
)yaml";

        UserConfig uc(userconf);

        REQUIRE(uc.getMailaddress() == "");
    }
}

TEST_CASE("User config old format", "[userconfig]") {

    SECTION("user config reads single line valid email") {
        std::string userconf =
            R"(address@mail.com
)";

        UserConfig uc(userconf);

        REQUIRE(uc.getMailaddress() == "address@mail.com");
    }

    SECTION("user config ignores single line invalid email") {
        std::string userconf =
            R"(1-invalid_mail_address!
)";

        UserConfig uc(userconf);

        REQUIRE(uc.getMailaddress() == "");
    }
}

TEST_CASE("User config malformed yaml does not abort", "[userconfig]") {
    // regression test for SECREVIEW L9: YAML exceptions from YAML::Load and the
    // typed as<>() conversions escaped the constructor uncaught and killed the
    // (setuid) tool with std::terminate

    SECTION("wrong scalar type falls back to defaults") {
        std::string userconf =
            R"yaml(
mail: address@mail.com
duration: 1w
)yaml";

        UserConfig uc(userconf);

        // the whole (broken) file is ignored, so valid fields reset as well
        REQUIRE(uc.getMailaddress() == "");
        REQUIRE(uc.getGroupname() == "");
        REQUIRE(uc.getDuration() == -1);
        REQUIRE(uc.getReminder() == -1);
    }

    SECTION("sequence value for string field falls back to defaults") {
        std::string userconf =
            R"yaml(
mail: [one, two]
)yaml";

        UserConfig uc(userconf);

        REQUIRE(uc.getMailaddress() == "");
    }

    SECTION("bool field with non-bool value falls back to default") {
        std::string userconf =
            R"yaml(
expirationmail: notabool
)yaml";

        UserConfig uc(userconf);

        REQUIRE(uc.getExpirationMail() == true);
    }

    SECTION("malformed yaml syntax falls back to defaults") {
        // tab used for indentation: hard YAML syntax error
        std::string userconf = "mail: address@mail.com\n\tduration: 5\n";

        UserConfig uc(userconf);

        REQUIRE(uc.getMailaddress() == "");
        REQUIRE(uc.getDuration() == -1);
    }

    SECTION("valid config still works after the try/catch was introduced") {
        std::string userconf =
            R"yaml(
mail: address@mail.com
duration: 10
reminder: 3
expirationmail: false
)yaml";

        UserConfig uc(userconf);

        REQUIRE(uc.getMailaddress() == "address@mail.com");
        REQUIRE(uc.getDuration() == 10);
        REQUIRE(uc.getReminder() == 3);
        REQUIRE(uc.getExpirationMail() == false);
    }
}
