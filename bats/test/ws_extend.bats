# Test suite for ws_extend
# AI disclosure: written as regression tests for the M7 option-injection fix

setup() {
    load 'test_helper/common-setup'
    _common_setup
    export PATH=$PATH:$PWD/bin
}

@test "ws_extend present" {
    which ws_extend
}

@test "ws_extend usage without arguments" {
    run ws_extend
    assert_failure
    assert_output --partial "Usage: ws_extend"
}

@test "ws_extend usage with wrong argument count" {
    run ws_extend onlyonearg
    assert_failure
    assert_output --partial "Usage: ws_extend"
}

@test "ws_extend leading-dash workspace is not an option for ws_allocate" {
    # Before the M7 fix, "ws_extend -h 3" forwarded "-h" into ws_allocate,
    # which printed its help text and exited 0. With "--" in the wrapper, the
    # name reaches ws_allocate as a positional argument: the call must fail
    # (nonexistent workspace or missing config) without showing help.
    run ws_extend -h 3
    assert_failure
    refute_output --partial "produce help message"
}

@test "ws_extend -F forwarding keeps options separated" {
    # 4-arg form with a leading-dash workspace name must likewise not be
    # parsed as options by ws_allocate.
    run ws_extend -F ws2 -h 3
    assert_failure
    refute_output --partial "produce help message"
}
