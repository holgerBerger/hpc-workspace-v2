#define CATCH_CONFIG_MAIN // This tells Catch to provide a main() - only do this in one cpp file
#include <catch2/catch_test_macros.hpp>
#include <string>

#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

#include "fmt/core.h"
#include "fmt/ostream.h"

#include <sys/types.h>
#include <unistd.h>

#include "../src/caps.h"
#include "../src/config.h"

bool debugflag = false;
bool traceflag = false;

// init caps here, when euid!=uid
Cap caps{};

////// MULTIPLE FILES ///////

TEST_CASE("config file: multiple files and order", "[config]")
{
    auto tmpbase = fs::temp_directory_path();
    auto id = getpid();
    auto basedirname = tmpbase / fs::path(fmt::format("wstest{}", id));

    fs::create_directories(basedirname / "ws.d");

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
        database: /tmp
        deleted: .removed
        spaces: [/tmp]
    ws2:
        database: /tmp
        deleted: .removed
        spaces: [/tmp/ws2-old]
)yaml");
    wsconf.close();

    // create a bad normal config file, V1 way
    std::ofstream wsconfbad(basedirname / "ws.conf-bad");
    fmt::println(wsconfbad,
                 R"yaml(
admins: [root]
clustername: old_ws_conf
adminmail: [root]
duration: 10
maxextensions: 1
smtphost: mailhost
default: ws2
workspaces:
    ws1:
        database: /tmp
        deleted: .removed
        spaces: [/tmp]
    ws2:
        database: /tmp
        deleted: .removed
        spaces: [/tmp/ws2-old]
)yaml");
    wsconfbad.close();

    // create new style multi file config

    std::ofstream wsconf1(basedirname / "ws.d" / "0-global.conf");
    fmt::println(wsconf1,
                 R"yaml(
admins: [root]
clustername: new_ws_conf
adminmail: [root]
dbgid: 2
dbuid: 2
duration: 10
smtphost: mailhost
default: ws2
)yaml");
    wsconf1.close();

    std::ofstream wsconf2(basedirname / "ws.d" / "1-ws2.conf");
    fmt::println(wsconf2,
                 R"yaml(
workspaces:
    ws2:
        database: /tmp
        deleted: .removed
        spaces: [/tmp/ws2]
)yaml");
    wsconf2.close();

    // write a file first with a name later in sorted list
    std::ofstream wsconf4(basedirname / "ws.d" / "3-ws3.conf");
    fmt::println(wsconf4,
                 R"yaml(
workspaces:
    ws3:
        database: /tmp
        deleted: .removed
        spaces: [/tmp/ws3-overwrite]
)yaml");
    wsconf4.close();

    std::ofstream wsconf3(basedirname / "ws.d" / "2-ws3.conf");
    fmt::println(wsconf3,
                 R"yaml(
workspaces:
    ws3:
        database: /tmp
        deleted: .removed
        spaces: [/tmp/ws3]
)yaml");
    wsconf3.close();

    SECTION("readconfig")
    {

        auto config = Config(std::vector<fs::path>{basedirname / "ws.conf"});

        REQUIRE(config.clustername() == "old_ws_conf");
        REQUIRE(config.dbuid() == 2);

        auto filesystem = config.getFsConfig("ws2");
        REQUIRE(filesystem.name == "ws2");
        REQUIRE(filesystem.spaces == vector<string>{"/tmp/ws2-old"});
        REQUIRE(config.isValid());

        // check if stop after first file works

        auto config2 = Config(std::vector<fs::path>{basedirname / "ws.conf", basedirname / "ws.d"});

        REQUIRE(config2.clustername() == "old_ws_conf");
        REQUIRE(config2.isValid());

        // check if multiple files are read and accumulated
        auto config3 = Config(std::vector<fs::path>{basedirname / "ws.d", basedirname / "ws.conf"});

        REQUIRE(config3.clustername() == "new_ws_conf");
        REQUIRE(config3.maxextensions() == 10); // default form code
        REQUIRE(config3.dbuid() == 2);
        REQUIRE(config3.dbgid() == 2);
        auto filesystem2 = config3.getFsConfig("ws2");
        auto filesystem3 = config3.getFsConfig("ws3");
        REQUIRE(filesystem2.name == "ws2");
        REQUIRE(filesystem3.name == "ws3");
        // check if sorting works
        REQUIRE(filesystem3.spaces == vector<string>{"/tmp/ws3-overwrite"});
        REQUIRE(config3.isValid());

        // check validation

        auto config4 = Config(std::vector<fs::path>{basedirname / "ws.conf-bad"});
        REQUIRE(config4.isValid() == false);

        // check bad file
        auto config5 = Config(std::vector<fs::path>{basedirname / "invalid-file"});
        REQUIRE(config5.isValid() == false);
    }

    // TODO: could use more tests for errors in config file
}

/////  PERMISSIONS AND ORDER /////

TEST_CASE("config file: permissions and order", "[config]")
{

    SECTION("canFind")
    {
        REQUIRE(canFind(std::vector<string>{"a", "b"}, string("a")) == true);
        REQUIRE(canFind(std::vector<string>{"a", "b"}, string("b")) == true);
        REQUIRE(canFind(std::vector<string>{"a"}, string("a")) == true);
        REQUIRE(canFind(std::vector<string>{"a", "b"}, string("c")) == false);
        REQUIRE(canFind(std::vector<string>{"a"}, string("c")) == false);
        REQUIRE(canFind(std::vector<string>{}, string("c")) == false);
    }

    SECTION("validFilesystems")
    {
        auto config = Config(std::string(R"(
default: third
admins: [e]
dbuid: 2
dbgid: 2
adminmail: [root]
clustername: test
workspaces:
    first:
        user_acl: [+a,-b,d]
        groupdefault: [gb]
        database: /tmp
        spaces: [/tmp]
        deleted: .del
    fourth:
        database: /tmp
        spaces: [/tmp]
        deleted: .del
filesystems:
    second:
        userdefault: [a]
        database: /tmp
        spaces: [/tmp]
        deleted: .del
    third:
        userdefault: [z,y]
        database: /tmp
        spaces: [/tmp]
        deleted: .del
)"));

        // see if workspace and filesystems are used
        REQUIRE(config.validFilesystems("c", std::vector<string>{}, ws::LIST) ==
                std::vector<string>{"third", "fourth", "second"});
        // see if userdefault and acl works
        REQUIRE(config.validFilesystems("a", std::vector<string>{}, ws::LIST) ==
                std::vector<string>{"second", "third", "first", "fourth"});
        // see if ACL works, default does not override ACL
        REQUIRE(config.validFilesystems("b", std::vector<string>{"gb"}, ws::LIST) ==
                std::vector<string>{"third", "fourth", "second"});
        // global, groupdefault, others
        REQUIRE(config.validFilesystems("d", std::vector<string>{"gb"}, ws::LIST) ==
                std::vector<string>{"first", "third", "fourth", "second"});
        // userdefault first, group second, multiple groups
        REQUIRE(config.validFilesystems("a", std::vector<string>{"gc", "gb"}, ws::LIST) ==
                std::vector<string>{"second", "first", "third", "fourth"});
        // user first, others, no denied, not only for first user, multiple groups
        REQUIRE(config.validFilesystems("y", std::vector<string>{"ga", "gc"}, ws::LIST) ==
                std::vector<string>{"third", "fourth", "second"});
        // admin user check, sees all filesystems
        REQUIRE(config.validFilesystems("e", std::vector<string>{}, ws::LIST) ==
                std::vector<string>{"third", "first", "fourth", "second"});
    }

    SECTION("hasAccess")
    {
        auto config = Config(std::string(R"(
admins: [d]
dbuid: 2
dbgid: 2
adminmail: [root]
clustername: test
workspaces:
    testacl:
        user_acl: [+a,-b,"+z:create","-y:list"]
        group_acl: [+bg]
        database: /tmp
        spaces: [/tmp]
        deleted: .del
filesystems:
    testnoacl:
        database: /tmp
        spaces: [/tmp]
        deleted: .del

)"));

        // workspace no acl
        REQUIRE(config.hasAccess("a", std::vector<string>{"cg", "dg"}, "testnoacl", ws::LIST) == true);
        // workspace with acl, through user
        REQUIRE(config.hasAccess("a", std::vector<string>{"cg", "dg"}, "testacl", ws::LIST) == true);
        // workspace with acl, unknown user
        REQUIRE(config.hasAccess("c", std::vector<string>{}, "testacl", ws::LIST) == false);
        // workspace with acl, through group
        REQUIRE(config.hasAccess("c", std::vector<string>{"bg"}, "testacl", ws::LIST) == true);
        // workspace with acl, through group but forbidden as user
        REQUIRE(config.hasAccess("b", std::vector<string>{"bg"}, "testacl", ws::LIST) == false);
        // admin user
        REQUIRE(config.hasAccess("d", std::vector<string>{""}, "testacl", ws::LIST) == true);

        // acl extended syntax
        REQUIRE(config.hasAccess("z", std::vector<string>{"cg", "dg"}, "testacl", ws::CREATE) == true);
        REQUIRE(config.hasAccess("z", std::vector<string>{"cg", "dg"}, "testacl", ws::LIST) == false);
        REQUIRE(config.hasAccess("y", std::vector<string>{"cg", "dg"}, "testacl", ws::LIST) == false);
        REQUIRE(config.hasAccess("y", std::vector<string>{"cg", "dg"}, "testacl", ws::CREATE) == true);
    }

    SECTION("isAdmin")
    {
        auto config = Config(std::string(R"(
admins: [d]
dbuid: 2
dbgid: 2
adminmail: [root]
clustername: test
workspaces:
    testacl:
        user_acl: [+a,-b]
        group_acl: [+bg]
        database: /tmp
        spaces: [/tmp]
        deleted: .del
filesystems:
    testnoacl:
        database: /tmp
        spaces: [/tmp]
        deleted: .del

)"));

        REQUIRE(config.hasAccess("d", std::vector<string>{""}, "testacl", ws::LIST) == true);
    }
}

TEST_CASE("config file: full sample", "[config]")
{

    auto config = Config(std::string(R"(
# this file illustrates and documents the workspace++ config files
# use ws_validate_config to validate a config file
clustername: aName              # mandatory, a name to identify the system
smtphost: localhost             # mandatory, a host accepting smtp connections to send emails
mail_from: noreply@mydomain.de  # sender address for reminders
default: lustre                 # mandatory, the default workspace to choose
duration: 10                    # mandatory, the max duration in days, if not specified in workspace
durationdefault: 1              # optional, if user does not give duration, this will be used
reminderdefault: 2              # optional, if set, users will always get mail, in doubt local mail to username
maxextensions: 1                # mandatory, the number of extensions, if not specified in workspace
pythonpath: /path/to/my/python  # optional, path which is appended to python search path for ws_list and other pythons scripts to find yaml
dbuid: 9999                     # mandatory
dbgid: 9999                     # mandatory
admins: [root]                  # list of admin users, for ws_list
adminmail: [root@localhost.com] # mail addresses for admins, used by ws_expirer to alert about bad situations
deldir_timeout: 3600            # maximum time in secs to delete a single workspace.
workspaces:                     # now the list of the workspaces
  lustre:                       # name of workspace as shown with ws_list -l
    keeptime: 1                 # mandatory, time in days to keep workspaces after they expired
    spaces: [/lustre1/ws, /lustre2/ws]  # mandatory, list of directories
    spaceselection: random      # "random" (default), "uid" (uid%#spaces), "gid" (gid%#spaces), "mostspace"
    deleted: .removed           # mandatory, will be appended to spaces and database
                                # to move deleted files to
    database: /lustre-db        # mandatory, the DB directory, this is where DB files will end
    duration: 30                # max duration, overwrites global value
    groupdefault: [inst1,inst2] # users of those groups will have this workspace as default
    userdefault: [user1]        # those users will have this workspace as default
    user_acl: [user1]           # as soon as user_acl or group_acl exist,
                                # the workspace is access restricted to those users/groups listed
    group_acl: [adm]
    maxextensions: 5            # maximum extensions allowed
    allocatable: yes             # do not allow new allocations in this workspace if no
    extendable: no              # do not allow extensions in this workspace if no
    restorable: no              # do not allow restores from this workspace if no
  nfs:                          # second workspace, minimum example
    comment: "what?"
    keeptime: 2                 # mandatory, time in days to keep workspaces after they expired
    database: /nfs-db
    spaces: [/nfs1/ws]
    deleted: .trash
)"));

    SECTION("check config")
    {

        REQUIRE(config.clustername() == "aName");
        REQUIRE(config.smtphost() == "localhost");
        REQUIRE(config.mailfrom() == "noreply@mydomain.de");
        REQUIRE(config.defaultworkspace() == "lustre");
        REQUIRE(config.maxduration() == 10);
        REQUIRE(config.durationdefault() == 1);
        REQUIRE(config.reminderdefault() == 2);
        REQUIRE(config.maxextensions() == 1);
        REQUIRE(config.dbuid() == 9999);
        REQUIRE(config.dbgid() == 9999);
        REQUIRE(config.admins() == vector<string>{"root"});
        REQUIRE(config.adminmail() == vector<string>{"root@localhost.com"});

        auto filesystem1 = config.getFsConfig("lustre");
        auto filesystem2 = config.getFsConfig("nfs");
        REQUIRE(filesystem1.name == "lustre");
        REQUIRE(filesystem2.name == "nfs");

        REQUIRE(filesystem1.keeptime == 1);
        REQUIRE(filesystem1.spaces == vector<string>{"/lustre1/ws", "/lustre2/ws"});
        REQUIRE(filesystem1.spaceselection == "random");
        REQUIRE(filesystem1.database == "/lustre-db");
        REQUIRE(filesystem1.maxduration == 30);
        REQUIRE(filesystem1.groupdefault == vector<string>{"inst1", "inst2"});
        REQUIRE(filesystem1.userdefault == vector<string>{"user1"});
        REQUIRE(filesystem1.user_acl == vector<string>{"user1"});
        REQUIRE(filesystem1.group_acl == vector<string>{"adm"});
        REQUIRE(filesystem1.maxextensions == 5);
        REQUIRE(filesystem1.allocatable == true);
        REQUIRE(filesystem1.extendable == false);
        REQUIRE(filesystem1.restorable == false);

        REQUIRE(filesystem2.comment == "what?");

        config.validate();

        REQUIRE(config.isValid());
    }
}
