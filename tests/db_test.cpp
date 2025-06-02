#define CATCH_CONFIG_MAIN // This tells Catch to provide a main() - only do this in one cpp file
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

#include "fmt/core.h"
#include "fmt/ostream.h"

#include "../src/caps.h"
#include "../src/dbv1.h"
#include "../src/user.h"

Cap caps{};

bool debugflag = false;
bool traceflag = false;

TEST_CASE("Database Test", "[db]")
{

    // create a stub/mockup  of everything
    //   - config
    //   - DB

    auto tmpbase = fs::temp_directory_path();
    auto id = getpid();
    auto basedirname = tmpbase / fs::path(fmt::format("wstest{}", id));

    fs::create_directories(basedirname / "ws.d");

    auto ws1dbname = basedirname / fs::path("ws1-db");
    auto ws2dbname = basedirname / fs::path("ws2-db");

    fs::create_directories(ws1dbname);
    fs::create_directories(ws2dbname);
    fs::create_directories(ws2dbname / fs::path(".removed"));

    // create a normal config file, V1 way
    std::ofstream wsconf(basedirname / "ws.conf");
    fmt::println(wsconf,
                 R"yaml(
admins: [root]
clustername: old_ws_conf
adminmail: [root]
dbgid: 2
dbuid: 2
duration: 10
maxextensions: 1
smtphost: mailhost
default: ws2
workspaces:
    ws1:
        database: {}
        deleted: .removed
        spaces: [/tmp]
    ws2:
        database: {}
        deleted: .removed
        spaces: [/tmp/ws2-old]
)yaml",
                 ws1dbname.string(), ws2dbname.string());
    wsconf.close();

    // create a DB entry
    std::ofstream wsentry1(ws1dbname / "user1-TEST1");
    fmt::println(wsentry1,
                 R"yaml(
workspace: /a/path
expiration: 1734701876
extensions: 1
reminder: 0
mailaddress: ""
comment: ""
)yaml");
    wsentry1.close();

    // create a DB entry
    std::ofstream wsentry2(ws1dbname / "user2-TEST1");
    fmt::println(wsentry2,
                 R"yaml(
workspace: /a/path11
expiration: 1734701876
extensions: 3
reminder: 0
group: group1             #  group workspace, entry with comment
mailaddress: ""
comment: ""
)yaml");
    wsentry2.close();

    // create a DB entry
    std::ofstream wsentry3(ws1dbname / "user2-TEST2");
    fmt::println(wsentry3,
                 R"yaml(
workspace: /a/path12
expiration: 1734701876
extensions: 3
reminder: 0
mailaddress: ""
comment: ""
)yaml");
    wsentry3.close();

    // create a DB entry
    std::ofstream wsentry4(ws2dbname / "user1-TEST1");
    fmt::println(wsentry4,
                 R"yaml(
workspace: /a/path21
expiration: 1734701876
extensions: 3
reminder: 0
mailaddress: ""
comment: ""
)yaml");
    wsentry4.close();

    // create a DB entry
    std::ofstream wsentry5(ws2dbname / "user2-TEST2");
    fmt::println(wsentry5,
                 R"yaml(
workspace: /a/path22
expiration: 1734701876
extensions: 3
reminder: 0
mailaddress: ""
comment: ""
)yaml");
    wsentry5.close();

    // create a BROKEN DB entry
    std::ofstream wsentry6(ws2dbname / "user3-BROKEN");
    fmt::print(wsentry6, R"yaml(works)yaml");
    wsentry6.close();

    // create a BROKEN DB entry
    std::ofstream wsentry7(ws2dbname / "user3-BROKEN2");
    fmt::print(wsentry7,
               R"yaml(yaml: dabadu
)yaml");
    wsentry7.close();

    // create a DB entry
    std::ofstream wsentry8(ws2dbname / ".removed" / "user2-TEST5-111111");
    fmt::println(wsentry8,
                 R"yaml(
workspace: /a/path22
expiration: 1734701876
extensions: 3
reminder: 0
mailaddress: ""
comment: ""
)yaml");
    wsentry8.close();

    auto config = Config(std::vector<fs::path>{basedirname / "ws.conf"});

    std::unique_ptr<Database> db1(config.openDB("ws1"));
    std::unique_ptr<Database> db2(config.openDB("ws2"));

    SECTION("list entries")
    {

        // test some patterns
        REQUIRE(db1->matchPattern("TEST*", "user1", vector<string>{}, false, false) == vector<string>{"user1-TEST1"});

        auto result1 = db1->matchPattern("*EST*", "user2", vector<string>{}, false, false);
        REQUIRE(((result1 == vector<string>{"user2-TEST2", "user2-TEST1"}) ||
                 (result1 == vector<string>{"user2-TEST1", "user2-TEST2"})));

        auto result2 = db1->matchPattern("*", "user2", vector<string>{}, false, false);
        REQUIRE(((result2 == vector<string>{"user2-TEST2", "user2-TEST1"}) ||
                 (result2 == vector<string>{"user2-TEST1", "user2-TEST2"})));

        REQUIRE(db1->matchPattern("*Pest*", "user1", vector<string>{}, false, false) == vector<string>{});
        REQUIRE(db2->matchPattern("T*T2", "user2", vector<string>{}, false, false) == vector<string>{"user2-TEST2"});
        // deleted workspace
        REQUIRE(db2->matchPattern("*", "user2", vector<string>{}, true, false) == vector<string>{"user2-TEST5-111111"});
        // group workspace
        REQUIRE(db1->matchPattern("*", "user1", vector<string>{"group1"}, false, true) ==
                vector<string>{"user2-TEST1"});
    }

    SECTION("read entry")
    {

        std::unique_ptr<DBEntry> entry(db1->readEntry("user1-TEST1", false));
        REQUIRE(entry != nullptr);
        REQUIRE(entry->getExpiration() == 1734701876);
        REQUIRE(entry->getWSPath() == "/a/path");

        // does not exist
        REQUIRE_THROWS([&]() { std::unique_ptr<DBEntry> entry2(db1->readEntry("user-TEST", false)); }());

        // is no yaml
        REQUIRE_THROWS([&]() { std::unique_ptr<DBEntry> entry3(db2->readEntry("user3-BROKEN", false)); }());

        // is yaml, but has no known entries, should give default
        std::unique_ptr<DBEntry> entry4(db2->readEntry("user3-BROKEN2", false));
        REQUIRE(entry4->getExtension() == 0);
    }

    SECTION("modify entry")
    {

        std::unique_ptr<DBEntry> entry(db1->readEntry("user1-TEST1", false));
        REQUIRE(entry->getExtension() == 1);
        REQUIRE_NOTHROW(entry->useExtension(entry->getExpiration() + 1, "mail@box.com", 5, "nice workspace"));
        REQUIRE(entry->getExtension() == 0);
        // no extensions left
        REQUIRE_THROWS(entry->useExtension(entry->getExpiration() + 2, "mail@box.com", 5, "nice workspace"));

        // read again from file
        std::unique_ptr<DBEntry> entry2(db1->readEntry("user1-TEST1", false));
        REQUIRE(entry->getMailaddress() == "mail@box.com");
    }
}

TEST_CASE("workspace creation test", "[db]")
{

    // create a stub/mockup  of everything
    //   - config
    //   - DB

    auto tmpbase = fs::temp_directory_path();
    auto id = getpid();
    auto basedirname = tmpbase / fs::path(fmt::format("wstest{}", id));

    fs::create_directories(basedirname / "ws.d");

    auto ws1dbname = basedirname / fs::path("ws1-db");
    auto ws2dbname = basedirname / fs::path("ws2-db");

    fs::create_directories(ws1dbname);
    fs::create_directories(ws2dbname);
    fs::create_directories(ws2dbname / fs::path(".removed"));

    // create a normal config file, V1 way
    std::ofstream wsconf(basedirname / "ws.conf");
    fmt::println(wsconf,
                 R"yaml(
admins: [root]
clustername: ws_conf
adminmail: [root]
dbgid: 2
dbuid: 2
duration: 10
maxextensions: 1
smtphost: mailhost
default: ws2
workspaces:
    ws1:
        database: {}
        deleted: .removed
        spaces: [/tmp,/var/tmp]
    ws2:
        database: {}
        deleted: .removed
        spaces: [/tmp/ws2-old]
)yaml",
                 ws1dbname.string(), ws2dbname.string());
    wsconf.close();

    auto config = Config(std::vector<fs::path>{basedirname / "ws.conf"});

    std::unique_ptr<Database> db1(config.openDB("ws1"));
    std::unique_ptr<Database> db2(config.openDB("ws2"));

    // create workspace dir
    auto wsdir = db1->createWorkspace("test1", "", false, "");

    REQUIRE(wsdir.size() > 0);
    REQUIRE(fs::exists(wsdir));
    REQUIRE(fs::is_directory(wsdir));
    // file permissions
    auto permissions = std::filesystem::status(wsdir).permissions();
    {
        using std::filesystem::perms;
        REQUIRE((permissions & perms::mask) == (perms::owner_read | perms::owner_write | perms::owner_exec));
    }

    // create DBentry for it
    db1->createEntry("user1-test1", wsdir, time(NULL), time(NULL) + 3601, 1, 7, "", "", "no comment");

    // see if we can match it
    REQUIRE(db1->matchPattern("test1", "user1", vector<string>{}, false, false) == vector<string>{"user1-test1"});

    // see if we can read it
    std::unique_ptr<DBEntry> entry(db1->readEntry("user1-test1", false));

    REQUIRE(entry->getComment() == "no comment");
    REQUIRE(entry->getExtension() == 7);

    // group workspace

    auto uname = user::getUsername();
    auto gname = user::getGroupname();
    wsdir = db1->createWorkspace("gtest2", uname, true, gname);

    REQUIRE(wsdir.size() > 0);
    REQUIRE(fs::exists(wsdir));
    REQUIRE(fs::is_directory(wsdir));

    // create DBentry for it
    db1->createEntry(uname + "-gtest2", wsdir, time(NULL), time(NULL) + 3601, 1, 7, gname, "", "no comment");

    // see if we can match it, with a non-existing user
    REQUIRE(db1->matchPattern("gtest2", "does-not-exist", vector<string>{gname}, false, true) ==
            vector<string>{uname + "-gtest2"});

    // see if we can read it
    std::unique_ptr<DBEntry> entry2(db1->readEntry(uname + "-gtest2", false));

    REQUIRE(entry2->getComment() == "no comment");
    REQUIRE(entry2->getExtension() == 7);

    // file permissions
    permissions = std::filesystem::status(wsdir).permissions();
    {
        using std::filesystem::perms;
        REQUIRE((permissions & perms::mask) == (perms::owner_all | perms::group_all | perms::set_gid));
    }
}
