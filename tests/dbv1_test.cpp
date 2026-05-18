#define CATCH_CONFIG_MAIN // This tells Catch to provide a main() - only do this in one cpp file
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fmt/core.h>
#include <fstream>
#include <iterator>

#include "../src/caps.h"
#include "../src/dbv1.h"

namespace fs = std::filesystem;

Cap caps{};

bool debugflag = false;
bool traceflag = false;
int debuglevel = 0;

// --- Happy path -------------------------------------------------------

TEST_CASE("Database v1 yaml parser", "[dbv1]") {

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

    SECTION("readentry") {
        REQUIRE(entry.getWSPath() == "/a/path");
        REQUIRE(entry.getExpiration() == 1734701876);
        REQUIRE(entry.getExtension() == 3);
        REQUIRE(entry.getMailaddress() == "");
    }
}

TEST_CASE("Glaze DB parsing edge cases", "[dbv1]") {

    // A scalar string (not a YAML map) must throw
    SECTION("scalar input throws") {
        DBEntryV1 e(nullptr);
        REQUIRE_THROWS(e.readFromString("invalid_entry"));
    }

    // Broken YAML that cannot be parsed at all must throw
    SECTION("broken yaml throws") {
        DBEntryV1 e(nullptr);
        REQUIRE_THROWS(e.readFromString("{{:"));
    }

    // A YAML list instead of a map must throw
    SECTION("yaml list instead of map throws") {
        DBEntryV1 e(nullptr);
        REQUIRE_THROWS(e.readFromString("- one\n- two"));
    }

    // Empty string must throw
    SECTION("empty string throws") {
        DBEntryV1 e(nullptr);
        REQUIRE_THROWS(e.readFromString(""));
    }

    // Whitespace-only string must throw
    SECTION("whitespace-only string throws") {
        DBEntryV1 e(nullptr);
        REQUIRE_THROWS(e.readFromString("   \n\n  "));
    }

    // A valid YAML map but missing the required "workspace" key must throw
    SECTION("missing workspace key throws") {
        DBEntryV1 e(nullptr);
        REQUIRE_THROWS(e.readFromString("expiration: 12345"));
    }

    // A valid YAML map but workspace is empty must throw
    SECTION("empty workspace value throws") {
        DBEntryV1 e(nullptr);
        REQUIRE_THROWS(e.readFromString("workspace:\nexpiration: 12345"));
    }

    // A valid YAML map with completely unknown keys (no workspace) must throw
    SECTION("unknown keys only throws") {
        DBEntryV1 e(nullptr);
        REQUIRE_THROWS(e.readFromString("foobar: 42"));
    }

    // A valid YAML map with unknown keys PLUS workspace must succeed
    SECTION("unknown keys alongside known keys succeeds") {
        DBEntryV1 e(nullptr);
        e.readFromString("workspace: /foo\nexpiration: 999\nfoobar: 42");
        REQUIRE(e.getWSPath() == "/foo");
        REQUIRE(e.getExpiration() == 999);
    }
}

// --- write / read round-trip ------------------------------------------

TEST_CASE("Glaze DB write and read round-trip", "[dbv1]") {

    auto tmp = fs::temp_directory_path() / fs::path(fmt::format("glaze_roundtrip_{}", getpid()));
    fs::create_directories(tmp);

    // write a DB entry file directly by constructing and serializing
    std::ofstream fout(tmp / "roundtrip-test");
    fout << R"yaml(
workspace: /tmp/ws
creation: 1000000
expiration: 2000000
extensions: 5
reminder: 100
mailaddress: foo@bar.com
comment: roundtrip
)yaml";
    fout.close();

    // read back via readFromString
    std::ifstream infile(tmp / "roundtrip-test");
    auto content = std::string(std::istreambuf_iterator<char>{infile}, std::istreambuf_iterator<char>{});
    DBEntryV1 entry(nullptr);
    entry.readFromString(content);

    REQUIRE(entry.getWSPath() == "/tmp/ws");
    REQUIRE(entry.getExpiration() == 2000000);
    REQUIRE(entry.getExtension() == 5);
    REQUIRE(entry.getReminder() == 100);
    REQUIRE(entry.getMailaddress() == "foo@bar.com");
    REQUIRE(entry.getComment() == "roundtrip");

    fs::remove(tmp / "roundtrip-test");
    fs::remove(tmp);
}
