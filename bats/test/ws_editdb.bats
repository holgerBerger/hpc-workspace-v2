setup() {
    load 'test_helper/common-setup'
    _common_setup
}

@test "ws_editdb present" {
    which ws_editdb
}

@test "ws_editdb print version" {
    run ws_editdb --version
    assert_output --partial "ws_editdb"
    assert_success
}

@test "ws_editdb print help" {
    run ws_editdb --help
    assert_output --partial "Usage"
    assert_success
}

@test "ws_editdb dryrun" {
    ws_allocate --config bats/ws.conf EDITTEST 1
    run ws_editdb --config bats/ws.conf --add-time 1 EDITTEST
    assert_output --partial "change expiration"
    assert_success
}

@test "ws_editdb not kidding" {
    run ws_editdb --config bats/ws.conf --not-kidding --add-time 1 EDITTEST
    assert_output --partial "change expiration"
    assert_success
    run ws_list --config bats/ws.conf  EDITTEST
    assert_output --regexp "(2 days)|1 days, 23"
    assert_success
}

@test "ws_editdb ensure-until" {
    run ws_editdb --config bats/ws.conf --not-kidding --ensure-until 2050-12-31 EDITTEST
    assert_output --partial "change expiration"
    assert_success
    run ws_list --config bats/ws.conf EDITTEST
    assert_output --partial "expiration time      : Sat Dec 31 23:59:59 2050"
    assert_success
}

@test "ws_editdb expire-by" {
    run ws_editdb --config bats/ws.conf --not-kidding --expire-by 2049-12-31 EDITTEST
    assert_output --partial "change expiration"
    assert_success
    run ws_list --config bats/ws.conf EDITTEST
    assert_output --partial "expiration time      : Fri Dec 31 00:00:00 2049"
    assert_success
    ws_release --config bats/ws.conf EDITTEST
}

# Test --add-time-expired functionality
@test "ws_editdb add-time-expired dryrun" {
    ws_allocate --config bats/ws.conf EXPIREDTEST 1
    run ws_editdb --config bats/ws.conf --add-time-expired 5 EXPIREDTEST
    assert_output --partial "change expired"
    assert_success
}

@test "ws_editdb add-time-expired not kidding" {
    run ws_editdb --config bats/ws.conf --not-kidding --add-time-expired 10 EXPIREDTEST
    assert_output --partial "change expired"
    assert_success
    ws_release --config bats/ws.conf EXPIREDTEST
}

# Test pattern matching
@test "ws_editdb pattern matching with wildcard" {
    ws_allocate --config bats/ws.conf PATTERN_TEST1 1
    ws_allocate --config bats/ws.conf PATTERN_TEST2 1
    ws_allocate --config bats/ws.conf OTHER_NAME 1
    run ws_editdb --config bats/ws.conf --add-time 1 "PATTERN_*"
    assert_output --partial "PATTERN_TEST1"
    assert_output --partial "PATTERN_TEST2"
    refute_output --partial "OTHER_NAME"
    assert_success
    ws_release --config bats/ws.conf PATTERN_TEST1
    ws_release --config bats/ws.conf PATTERN_TEST2
    ws_release --config bats/ws.conf OTHER_NAME
}

@test "ws_editdb pattern matching exact name" {
    ws_allocate --config bats/ws.conf EXACT_MATCH 1
    ws_allocate --config bats/ws.conf EXACT_MATCH_OTHER 1
    run ws_editdb --config bats/ws.conf --add-time 1 "EXACT_MATCH"
    assert_output --partial "EXACT_MATCH"
    refute_output --partial "EXACT_MATCH_OTHER"
    assert_success
    ws_release --config bats/ws.conf EXACT_MATCH
    ws_release --config bats/ws.conf EXACT_MATCH_OTHER
}

# Test conflicting options
@test "ws_editdb multiple adds" {
    ws_allocate --config bats/ws.conf CONFLICT_TEST 1
    run ws_editdb --config bats/ws.conf --not-kidding --expire-by 2049-12-31 --add-time 1 CONFLICT_TEST
    assert_output --partial "Only one"
    assert_failure
    ws_release --config bats/ws.conf CONFLICT_TEST
}

@test "ws_editdb add-time and add-time-expired conflict" {
    ws_allocate --config bats/ws.conf CONFLICT_TEST2 1
    run ws_editdb --config bats/ws.conf --add-time 1 --add-time-expired 1 CONFLICT_TEST2
    assert_output --partial "Only one"
    assert_failure
    ws_release --config bats/ws.conf CONFLICT_TEST2
}

@test "ws_editdb ensure-until and expire-by conflict" {
    ws_allocate --config bats/ws.conf CONFLICT_TEST3 1
    run ws_editdb --config bats/ws.conf --ensure-until 2050-12-31 --expire-by 2049-12-31 CONFLICT_TEST3
    assert_output --partial "Only one"
    assert_failure
    ws_release --config bats/ws.conf CONFLICT_TEST3
}

@test "ws_editdb add-time and ensure-until conflict" {
    ws_allocate --config bats/ws.conf CONFLICT_TEST4 1
    run ws_editdb --config bats/ws.conf --add-time 1 --ensure-until 2050-12-31 CONFLICT_TEST4
    assert_output --partial "Only one"
    assert_failure
    ws_release --config bats/ws.conf CONFLICT_TEST4
}

# Test dry-run vs not-kidding conflict
@test "ws_editdb dry-run and not-kidding conflict" {
    ws_allocate --config bats/ws.conf DRYRUN_TEST 1
    run ws_editdb --config bats/ws.conf --dry-run --not-kidding --add-time 1 DRYRUN_TEST
    assert_output --partial "either"
    assert_success
    ws_release --config bats/ws.conf DRYRUN_TEST
}

# Test date parsing
@test "ws_editdb invalid date format" {
    ws_allocate --config bats/ws.conf DATETEST 1
    run ws_editdb --config bats/ws.conf --expire-by "invalid-date" DATETEST
    assert_output --partial "parsing failed"
    assert_failure
    ws_release --config bats/ws.conf DATETEST
}

@test "ws_editdb valid date formats" {
    ws_allocate --config bats/ws.conf DATETEST2 1
    run ws_editdb --config bats/ws.conf --ensure-until 2048-01-15 DATETEST2
    assert_output --partial "change expiration"
    assert_success
    ws_release --config bats/ws.conf DATETEST2
}

# Test ensure-until behavior (only extends, doesn't shrink)
@test "ws_editdb ensure-until does not shrink" {
    ws_allocate --config bats/ws.conf ENSURE_TEST 1
    # First extend it far into the future
    ws_editdb --config bats/ws.conf --not-kidding --ensure-until 2060-12-31 ENSURE_TEST
    # Try to "ensure" earlier date - should not change anything
    run ws_editdb --config bats/ws.conf --not-kidding --ensure-until 2040-12-31 ENSURE_TEST
    refute_output --partial "change expiration"
    assert_success
    ws_release --config bats/ws.conf ENSURE_TEST
}

# Test expire-by behavior (only shrinks, doesn't extend)
@test "ws_editdb expire-by does not extend" {
    ws_allocate --config bats/ws.conf EXPIRE_TEST 1
    # Try to expire by far future date - should not change anything since workspace expires sooner
    run ws_editdb --config bats/ws.conf --not-kidding --expire-by 2060-12-31 EXPIRE_TEST
    refute_output --partial "change expiration"
    assert_success
    ws_release --config bats/ws.conf EXPIRE_TEST
}

# Test negative add-time
@test "ws_editdb negative add-time" {
    ws_allocate --config bats/ws.conf NEGATIVE_TIME 1
    run ws_editdb --config bats/ws.conf --not-kidding --add-time -1 NEGATIVE_TIME
    assert_output --partial "change expiration"
    assert_success
    ws_release --config bats/ws.conf NEGATIVE_TIME
}

# Test with no pattern (should match all)
@test "ws_editdb no pattern matches all" {
    ws_allocate --config bats/ws.conf NOPATTERN1 1
    ws_allocate --config bats/ws.conf NOPATTERN2 1
    run ws_editdb --config bats/ws.conf --add-time 1
    assert_output --partial "NOPATTERN1"
    assert_output --partial "NOPATTERN2"
    assert_success
    ws_release --config bats/ws.conf NOPATTERN1
    ws_release --config bats/ws.conf NOPATTERN2
}

# Test output format includes workspace path
@test "ws_editdb output shows workspace path" {
    ws_allocate --config bats/ws.conf PATHTEST 1
    run ws_editdb --config bats/ws.conf --add-time 1 PATHTEST
    assert_output --regexp "Id:.*\\(.*PATHTEST.*\\)"
    assert_success
    ws_release --config bats/ws.conf PATHTEST
}

# Test dry-run is default
@test "ws_editdb dry-run is default" {
    ws_allocate --config bats/ws.conf DRYDEFAULT 1
    run ws_editdb --config bats/ws.conf --add-time 1 DRYDEFAULT
    assert_output --partial "Actions that would be performed"
    assert_success
    # Verify nothing actually changed
    run ws_list --config bats/ws.conf DRYDEFAULT
    assert_output --regexp "(1 day)|(0 days, 23 hours)"
    assert_success
    ws_release --config bats/ws.conf DRYDEFAULT
}

# Test filesystem option
@test "ws_editdb with filesystem option" {
    ws_allocate --config bats/ws.conf FSTEST 1
    run ws_editdb --config bats/ws.conf --filesystem lustre01 --add-time 1 FSTEST
    assert_success
    ws_release --config bats/ws.conf FSTEST
}

# Test zero add-time (should do nothing)
@test "ws_editdb zero add-time" {
    ws_allocate --config bats/ws.conf ZEROTEST 1
    run ws_editdb --config bats/ws.conf --add-time 0 ZEROTEST
    refute_output --partial "change expiration"
    assert_success
    ws_release --config bats/ws.conf ZEROTEST
}

# Test large add-time value
@test "ws_editdb large add-time" {
    ws_allocate --config bats/ws.conf LARGETEST 1
    run ws_editdb --config bats/ws.conf --not-kidding --add-time 365 LARGETEST
    assert_output --partial "change expiration"
    assert_success
    ws_release --config bats/ws.conf LARGETEST
}

# Test that non-existent workspace pattern returns gracefully
@test "ws_editdb non-existent workspace" {
    run ws_editdb --config bats/ws.conf --add-time 1 "DOESNOTEXIST12345"
    refute_output --partial "change expiration"
    assert_success
}

# Test multiple workspaces modification
@test "ws_editdb modify multiple workspaces" {
    ws_allocate --config bats/ws.conf MULTI1 1
    ws_allocate --config bats/ws.conf MULTI2 1
    ws_allocate --config bats/ws.conf MULTI3 1
    run ws_editdb --config bats/ws.conf --not-kidding --add-time 2 "MULTI*"
    assert_output --partial "MULTI1"
    assert_output --partial "MULTI2"
    assert_output --partial "MULTI3"
    assert_success
    # Verify all were modified
    run ws_list --config bats/ws.conf MULTI1
    assert_output --regexp "(3 days)|2 days, 23"
    assert_success
    ws_release --config bats/ws.conf MULTI1
    ws_release --config bats/ws.conf MULTI2
    ws_release --config bats/ws.conf MULTI3
}

# Test with expired workspaces flag
@test "ws_editdb with expired flag" {
    ws_allocate --config bats/ws.conf EXPFLAG 1
    run ws_editdb --config bats/ws.conf --expired --add-time 1 EXPFLAG
    assert_success
    ws_release --config bats/ws.conf EXPFLAG
}

# Test ensure-until with past date
@test "ws_editdb ensure-until with past date" {
    ws_allocate --config bats/ws.conf PASTDATE 1
    run ws_editdb --config bats/ws.conf --not-kidding --ensure-until 2020-01-01 PASTDATE
    refute_output --partial "change expiration"
    assert_success
    ws_release --config bats/ws.conf PASTDATE
}

# Test expire-by with past date
@test "ws_editdb expire-by with past date" {
    ws_allocate --config bats/ws.conf PASTEXPIRE 1
    run ws_editdb --config bats/ws.conf --not-kidding --expire-by 2020-01-01 PASTEXPIRE
    assert_output --partial "change expiration"
    assert_success
    ws_release --config bats/ws.conf PASTEXPIRE
}
