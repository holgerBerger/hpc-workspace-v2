#include <algorithm>
#include <string>
#include <vector>

#include <grp.h>
#include <pwd.h>
#include <unistd.h>

#include <catch2/catch_test_macros.hpp>

#include "../src/caps.h"
#include "../src/user.h"

// these globals are referenced by ws_common; provide the same definitions the other
// unit-test binaries use so this binary links without pulling in a real logging setup
bool debugflag = false;
bool traceflag = false;
int debuglevel = 0;

// init caps here, when euid!=uid
Cap caps{};

TEST_CASE("user::exists", "[user]") {
    // "root" is guaranteed present in the passwd database
    REQUIRE(user::exists("root"));
    // a name that can not be a valid POSIX user must be absent
    REQUIRE_FALSE(user::exists("this_user_does_not_exist_9f3a"));
    REQUIRE_FALSE(user::exists(""));
}

TEST_CASE("user::getUsername", "[user]") {
    const std::string name = user::getUsername();
    REQUIRE_FALSE(name.empty());
    // must match the passwd entry for the real user id of this process
    const struct passwd* pw = getpwuid(getuid());
    REQUIRE(pw != nullptr);
    REQUIRE(name == std::string(pw->pw_name));
}

TEST_CASE("user::getUserhome", "[user]") {
    SECTION("current user") {
        const struct passwd* pw = getpwuid(getuid());
        REQUIRE(pw != nullptr);
        REQUIRE(user::getUserhome() == std::string(pw->pw_dir));
    }

    SECTION("named user") {
        const struct passwd* root = getpwnam("root");
        REQUIRE(root != nullptr);
        REQUIRE(user::getUserhome("root") == std::string(root->pw_dir));
    }

    SECTION("unknown user returns empty, not a crash") {
        // regression: getUserhome(name) must not abort for a name absent from passwd
        REQUIRE(user::getUserhome("this_user_does_not_exist_9f3a") == "");
    }
}

TEST_CASE("user::isRoot", "[user]") {
    // deterministically tied to the real uid of the running process (tests run in user mode)
    REQUIRE(user::isRoot() == (getuid() == 0));
}

TEST_CASE("user::setuid checks", "[user]") {
    REQUIRE(user::isnotSetuid() == (getuid() == geteuid()));
    REQUIRE(user::isSetuid() == (getuid() != geteuid()));
    // the two are exact complements
    REQUIRE(user::isSetuid() != user::isnotSetuid());
}

TEST_CASE("user::getGroupname", "[user]") {
    const std::string grp = user::getGroupname();
    REQUIRE_FALSE(grp.empty());
    const struct group* g = getgrgid(getegid());
    REQUIRE(g != nullptr);
    REQUIRE(grp == std::string(g->gr_name));
}

TEST_CASE("user::getGrouplist", "[user]") {
    const std::vector<std::string> groups = user::getGrouplist();
    // every returned name must be resolvable in the group database
    for (const auto& g : groups) {
        REQUIRE(getgrnam(g.c_str()) != nullptr);
    }
}

TEST_CASE("user::getUserGroupList", "[user]") {
    SECTION("known user") {
        const std::vector<std::string> groups = user::getUserGroupList("root");
        REQUIRE_FALSE(groups.empty());
        // root's primary group is "root", so it must appear in the list
        REQUIRE(std::find(groups.begin(), groups.end(), "root") != groups.end());
    }

    SECTION("unknown user returns empty") {
        REQUIRE(user::getUserGroupList("this_user_does_not_exist_9f3a").empty());
    }
}
