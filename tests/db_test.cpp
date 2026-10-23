#define CATCH_CONFIG_MAIN // This tells Catch to provide a main() - only do this in one cpp file
#include <catch2/catch_test_macros.hpp>

#include <csignal>
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
int debuglevel = 0;

TEST_CASE("Database Test", "[db]") {

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

    utils::writeFile(ws1dbname / ".ws_db_magic", "ws1");
    utils::writeFile(ws2dbname / ".ws_db_magic", "ws2");

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

    SECTION("list entries") {

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

    SECTION("read entry") {

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

    SECTION("username field") {

        // legacy entries without `user` field fall back to the dash heuristic
        std::unique_ptr<DBEntry> legacy(db1->readEntry("user1-TEST1", false));
        REQUIRE(legacy->getUsername() == "user1");

        // modifying and rewriting a legacy entry must NOT persist a guessed owner
        REQUIRE_NOTHROW(legacy->useExtension(legacy->getExpiration() + 1, "x@y.z", 0, "keep"));
        std::string legacyfile = utils::getFileContents((ws1dbname / "user1-TEST1").string());
        REQUIRE(legacyfile.find("user:") == string::npos);
        std::unique_ptr<DBEntry> legacy2(db1->readEntry("user1-TEST1", false));
        REQUIRE(legacy2->getUsername() == "user1");

        // a stored `user` field takes precedence over the heuristic: for id "aa-bb-cc"
        // the heuristic would resolve to "aa" (no such system user -> first dash chunk),
        // the field stores the true owner "aa-bb"
        {
            std::ofstream e(ws1dbname / "aa-bb-cc");
            fmt::println(e, "workspace: /a/path3\nuser: aa-bb\nexpiration: 1734701876\nextensions: 0\nreminder: 0\n");
        }
        std::unique_ptr<DBEntry> withuser(db1->readEntry("aa-bb-cc", false));
        REQUIRE(withuser->getUsername() == "aa-bb");
    }

    SECTION("modify entry") {

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

TEST_CASE("rename entries in deleted area", "[db]") {

    // own directory tree, so renames do not interfere with the other test cases
    auto basedirname = fs::temp_directory_path() / fs::path(fmt::format("wsrename{}", getpid()));
    fs::remove_all(basedirname);

    auto dbdir = basedirname / "db";
    auto spacedir = basedirname / "space";
    auto db_removed = dbdir / ".removed";
    auto ws_removed = spacedir / ".removed";
    fs::create_directories(db_removed);
    fs::create_directories(ws_removed);
    utils::writeFile(dbdir / ".ws_db_magic", "ws1");

    std::ofstream wsconf(basedirname / "ws.conf");
    fmt::println(wsconf,
                 R"yaml(
admins: [root]
clustername: rename_test
adminmail: [root]
dbgid: 2
dbuid: 2
duration: 10
maxextensions: 1
smtphost: mailhost
default: ws1
workspaces:
    ws1:
        database: {}
        deleted: .removed
        spaces: [{}]
)yaml",
                 dbdir.string(), spacedir.string());
    wsconf.close();

    // DB entry "<base>-<ts>" in deleted area, workspace path points into spacedir
    auto make_entry = [&](const std::string& base, const std::string& ts, const std::string& extra) {
        std::ofstream e(db_removed / (base + "-" + ts));
        fmt::println(e,
                     R"yaml(
workspace: {}
expiration: 1734701876
extensions: 3
reminder: 0
mailaddress: ""
comment: "{}"
{}
)yaml",
                     (spacedir / base).string(), ts, extra);
        e.close();
    };
    // workspace directory in deleted area with a marker file
    auto make_wsdir = [&](const std::string& name) {
        fs::create_directories(ws_removed / name);
        utils::writeFile(ws_removed / name / "marker", name);
    };

    auto config = Config(std::vector<fs::path>{basedirname / "ws.conf"});
    std::unique_ptr<Database> db(config.openDB("ws1"));

    SECTION("expiredIdFor finds collision free names") {
        make_entry("u-A", "100", "");
        make_wsdir("u-A-100");
        std::unique_ptr<DBEntry> entry(db->readEntry("u-A-100", true));

        REQUIRE(entry->expiredIdFor(5000, {}) == "u-A-5000");

        // DB entry with target name exists
        utils::writeFile(db_removed / "u-A-5000", "x");
        REQUIRE(entry->expiredIdFor(5000, {}) == "u-A-5001");

        // only workspace directory with target name exists
        make_wsdir("u-A-5001");
        REQUIRE(entry->expiredIdFor(5000, {}) == "u-A-5002");

        // name reserved by caller (planned in same run)
        REQUIRE(entry->expiredIdFor(5000, {"u-A-5002"}) == "u-A-5003");

        // own name reached first: no rename needed
        utils::writeFile(db_removed / "u-A-99", "x");
        REQUIRE(entry->expiredIdFor(99, {}) == "u-A-100");
        REQUIRE(entry->expiredIdFor(100, {}) == "u-A-100");

        // implausible boundary
        REQUIRE_THROWS(entry->expiredIdFor(-5, {}));
    }

    SECTION("renameAsExpired moves DB entry and workspace") {
        make_entry("u-B", "100", "");
        make_wsdir("u-B-100");
        std::unique_ptr<DBEntry> entry(db->readEntry("u-B-100", true));

        REQUIRE_NOTHROW(entry->renameAsExpired("u-B-200"));
        REQUIRE(entry->getId() == "u-B-200");
        REQUIRE(fs::exists(db_removed / "u-B-200"));
        REQUIRE_FALSE(fs::exists(db_removed / "u-B-100"));
        REQUIRE(fs::exists(ws_removed / "u-B-200" / "marker"));
        REQUIRE(utils::getFileContents((ws_removed / "u-B-200" / "marker").c_str()) == "u-B-100");
        REQUIRE_FALSE(fs::exists(ws_removed / "u-B-100"));

        // new name is a valid entry
        std::unique_ptr<DBEntry> entry2(db->readEntry("u-B-200", true));
        REQUIRE(entry2->getComment() == "100");

        // same name: no-op
        REQUIRE_NOTHROW(entry2->renameAsExpired("u-B-200"));
        REQUIRE(fs::exists(db_removed / "u-B-200"));
    }

    SECTION("renameAsExpired never overwrites") {
        // DB entry target exists
        make_entry("u-C", "100", "");
        make_wsdir("u-C-100");
        make_entry("u-C", "200", "");
        std::unique_ptr<DBEntry> entry(db->readEntry("u-C-100", true));
        REQUIRE_THROWS(entry->renameAsExpired("u-C-200"));
        REQUIRE(entry->getId() == "u-C-100");
        REQUIRE(fs::exists(db_removed / "u-C-100"));
        REQUIRE(fs::exists(ws_removed / "u-C-100" / "marker"));
        std::unique_ptr<DBEntry> other(db->readEntry("u-C-200", true));
        REQUIRE(other->getComment() == "200");

        // workspace target exists, even an empty directory (rename would replace it)
        make_entry("u-D", "100", "");
        make_wsdir("u-D-100");
        fs::create_directories(ws_removed / "u-D-200");
        std::unique_ptr<DBEntry> entry2(db->readEntry("u-D-100", true));
        REQUIRE_THROWS(entry2->renameAsExpired("u-D-200"));
        REQUIRE(entry2->getId() == "u-D-100");
        REQUIRE(fs::exists(db_removed / "u-D-100"));
        REQUIRE_FALSE(fs::exists(db_removed / "u-D-200"));
        REQUIRE(fs::exists(ws_removed / "u-D-100" / "marker"));
    }

    SECTION("renameAsExpired rolls back DB rename if workspace can not be moved") {
        // root ignores directory permissions, so this can only be provoked as normal user
        if (geteuid() != 0) {
            make_entry("u-G", "100", "");
            make_wsdir("u-G-100");
            std::unique_ptr<DBEntry> entry(db->readEntry("u-G-100", true));

            fs::permissions(ws_removed, fs::perms::owner_read | fs::perms::owner_exec);
            // CHECK instead of REQUIRE, to always restore permissions
            CHECK_THROWS(entry->renameAsExpired("u-G-200"));
            fs::permissions(ws_removed, fs::perms::owner_all);

            REQUIRE(entry->getId() == "u-G-100");
            REQUIRE(fs::exists(db_removed / "u-G-100"));
            REQUIRE_FALSE(fs::exists(db_removed / "u-G-200"));
            REQUIRE(fs::exists(ws_removed / "u-G-100" / "marker"));

            // entry is still usable with its old name after rollback
            REQUIRE_NOTHROW(entry->renameAsExpired("u-G-200"));
            REQUIRE(fs::exists(db_removed / "u-G-200"));
            REQUIRE(fs::exists(ws_removed / "u-G-200" / "marker"));
        }
    }

    SECTION("renameAsExpired with missing workspace directory renames DB entry only") {
        make_entry("u-F", "100", "");
        std::unique_ptr<DBEntry> entry(db->readEntry("u-F-100", true));
        REQUIRE_NOTHROW(entry->renameAsExpired("u-F-200"));
        REQUIRE(fs::exists(db_removed / "u-F-200"));
        REQUIRE_FALSE(fs::exists(db_removed / "u-F-100"));
    }

    SECTION("renameAsExpired refuses invalid requests") {
        // released entries carry the release time in their name
        make_entry("u-E", "100", "released: 1600000000");
        make_wsdir("u-E-100");
        std::unique_ptr<DBEntry> entry(db->readEntry("u-E-100", true));
        REQUIRE_THROWS(entry->renameAsExpired("u-E-200"));
        REQUIRE(fs::exists(db_removed / "u-E-100"));
        REQUIRE(fs::exists(ws_removed / "u-E-100"));

        // new id must keep the base and have a numeric timestamp
        make_entry("u-H", "100", "");
        std::unique_ptr<DBEntry> entry2(db->readEntry("u-H-100", true));
        REQUIRE_THROWS(entry2->renameAsExpired("u-X-200"));
        REQUIRE_THROWS(entry2->renameAsExpired("u-H-200x"));
        REQUIRE_THROWS(entry2->renameAsExpired("u-H-"));
        REQUIRE(fs::exists(db_removed / "u-H-100"));

        // id without timestamp suffix
        utils::writeFile(db_removed / "ubroken", "workspace: /x\nexpiration: 1734701876\n");
        std::unique_ptr<DBEntry> entry3(db->readEntry("ubroken", true));
        REQUIRE_THROWS(entry3->expiredIdFor(5000, {}));
        REQUIRE_THROWS(entry3->renameAsExpired("ubroken-5000"));
        REQUIRE(fs::exists(db_removed / "ubroken"));
    }

    SECTION("expire never overwrites deleted entries") {
        // a live entry: file in dbdir, workspace directory in spacedir
        auto make_live = [&](const std::string& base, const std::string& tag) {
            std::ofstream e(dbdir / base);
            fmt::println(e,
                         R"yaml(
workspace: {}
expiration: 1734701876
extensions: 3
reminder: 0
mailaddress: ""
comment: "{}"
)yaml",
                         (spacedir / base).string(), tag);
            e.close();
            fs::create_directories(spacedir / base);
        };

        // no collision: name is used as-is
        make_live("u-X1", "first");
        {
            std::unique_ptr<DBEntry> entry(db->readEntry("u-X1", false));
            REQUIRE(entry->expire("1000") == "1000");
        }
        REQUIRE_FALSE(fs::exists(dbdir / "u-X1"));
        REQUIRE(fs::exists(db_removed / "u-X1-1000"));

        // DB entry with that name already in deleted area: bump, never overwrite
        make_live("u-X1", "second");
        {
            std::unique_ptr<DBEntry> entry(db->readEntry("u-X1", false));
            REQUIRE(entry->expire("1000") == "1001");
        }
        std::unique_ptr<DBEntry> old(db->readEntry("u-X1-1000", true));
        REQUIRE(old->getComment() == "first"); // the pre-fix silent truncate is the bug pinned here
        std::unique_ptr<DBEntry> now(db->readEntry("u-X1-1001", true));
        REQUIRE(now->getComment() == "second");

        // only the workspace directory with that name exists (split-brain from an old move):
        // DB entry and workspace must end up with the same suffix
        make_live("u-X2", "wscollide");
        fs::create_directories(ws_removed / "u-X2-2000");
        {
            std::unique_ptr<DBEntry> entry(db->readEntry("u-X2", false));
            REQUIRE(entry->expire("2000") == "2001");
        }
        REQUIRE(fs::exists(db_removed / "u-X2-2001"));
        REQUIRE_FALSE(fs::exists(db_removed / "u-X2-2000"));

        // garbage timestamps are refused and leave the live entry alone
        make_live("u-X3", "garbage");
        {
            std::unique_ptr<DBEntry> entry(db->readEntry("u-X3", false));
            REQUIRE_THROWS_AS(entry->expire("abc"), DatabaseException);
            REQUIRE_THROWS_AS(entry->expire(""), DatabaseException);
            REQUIRE_THROWS_AS(entry->expire("12a"), DatabaseException);
            REQUIRE(fs::exists(dbdir / "u-X3"));
            REQUIRE_FALSE(fs::exists(db_removed / "u-X3-abc"));
        }
    }

    SECTION("writeEntry restores the previous SIGINT handler") {
        // writeEntry ignores SIGINT while writing and must hand back whatever handler the
        // caller had, not clobber it with SIG_DFL (previously leaked on early exits/throws)
        std::ofstream e(dbdir / "u-SIG");
        fmt::println(e,
                     R"yaml(
workspace: {}
expiration: 1734701876
extensions: 3
reminder: 0
mailaddress: ""
comment: "sigint"
)yaml",
                     (spacedir / "u-SIG").string());
        e.close();
        fs::create_directories(spacedir / "u-SIG");

        void (*dummy)(int) = +[](int) {};
        void (*installed)(int) = signal(SIGINT, dummy);
        std::unique_ptr<DBEntry> entry(db->readEntry("u-SIG", false));
        REQUIRE_NOTHROW(entry->writeEntry());
        void (*after)(int) = signal(SIGINT, installed); // fetch and restore
        REQUIRE(after == dummy);
    }

    fs::permissions(ws_removed, fs::perms::owner_all);
    fs::remove_all(basedirname);
}

TEST_CASE("workspace creation test", "[db]") {

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

    utils::writeFile(ws1dbname / ".ws_db_magic", "ws1");
    utils::writeFile(ws2dbname / ".ws_db_magic", "ws2");

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
    auto wsdir = db1->createWorkspace("test1", "", false, false, "");

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
    db1->createEntry("user1-test1", "user1", wsdir, time(NULL), time(NULL) + 3601, 1, 7, false, "", "", "no comment");

    // see if we can match it
    REQUIRE(db1->matchPattern("test1", "user1", vector<string>{}, false, false) == vector<string>{"user1-test1"});

    // see if we can read it
    std::unique_ptr<DBEntry> entry(db1->readEntry("user1-test1", false));

    REQUIRE(entry->getComment() == "no comment");
    REQUIRE(entry->getExtension() == 7);
    // the exact owner must be stored in the `user` field of the entry file
    REQUIRE(entry->getUsername() == "user1");
    REQUIRE(utils::getFileContents((ws1dbname / "user1-test1").string()).find("user: user1") != string::npos);

    // group workspace

    auto uname = user::getUsername();
    auto gname = user::getGroupname();
    wsdir = db1->createWorkspace("gtest2", uname, true, true, gname);

    REQUIRE(wsdir.size() > 0);
    REQUIRE(fs::exists(wsdir));
    REQUIRE(fs::is_directory(wsdir));

    // create DBentry for it
    db1->createEntry(uname + "-gtest2", uname, wsdir, time(NULL), time(NULL) + 3601, 1, 7, true, gname, "",
                     "no comment");

    // see if we can match it, with a non-existing user
    REQUIRE(db1->matchPattern("gtest2", "does-not-exist", vector<string>{gname}, false, true) ==
            vector<string>{uname + "-gtest2"});

    // see if we can read it
    std::unique_ptr<DBEntry> entry2(db1->readEntry(uname + "-gtest2", false));

    REQUIRE(entry2->getComment() == "no comment");
    REQUIRE(entry2->getExtension() == 7);
    REQUIRE(entry2->getUsername() == uname);

    // file permissions
    permissions = std::filesystem::status(wsdir).permissions();
    {
        using std::filesystem::perms;
        REQUIRE((permissions & perms::mask) == (perms::owner_all | perms::group_all | perms::set_gid));
    }
}
