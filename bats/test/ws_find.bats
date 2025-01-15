setup() {
    load 'test_helper/common-setup'
    _common_setup
    ws_name="bats_workspace_test"
    export ws_name
}

@test "ws_find present" {
    which ws_find
}

# bats test_tags=broken:v1-5-0
@test "ws_find print version" {
    run ws_find --version
    assert_output --partial "workspace"
    assert_success
}

@test "ws_find print help" {
    run ws_find --help
    assert_output --partial "Usage"
    assert_success
}

@test "ws_find finds directory" {
    rm -f /tmp/ws/ws2-db/${USER}-$ws_name
    wsdir1=$(ws_allocate --config bats/ws.conf $ws_name)
    assert_dir_exist $wsdir1
    wsdir2=$(ws_find --config bats/ws.conf $ws_name)
    assert_equal "$wsdir1" "$wsdir2"
}

@test "ws_find with filesystem, bad file" {
    ws_allocate --config bats/ws.conf -F ws1 WS1TEST_BAD
    cp /dev/null /tmp/ws/ws1-db/${USER}-WS1TEST_BAD
    run ws_find --config bats/ws.conf -F ws1 WS1TEST_BAD
    assert_output --partial Error
    assert_failure
    rm -f /tmp/ws/ws1-db/${USER}-WS1TEST_BAD
}

@test "ws_find in non default workspace" {
    ws_allocate --config bats/ws.conf -F ws1 WS1TEST
    run ws_find --config bats/ws.conf WS1TEST
    assert_output --partial WS1TEST
    assert_success
    rm -f /tmp/ws/ws1-db/${USER}-WS1TEST
}

@test "ws_find with bad workspace" {
    run ws_find --config bats/ws.conf DOESNOTEXIST
    assert_failure
}

@test "ws_find with good filesystem" {
    ws_allocate --config bats/ws.conf -F ws1 WS1TEST_GOOD
    run ws_find --config bats/ws.conf -F ws1 WS1TEST_GOOD
    assert_success
    rm -f /tmp/ws/ws1-db/${USER}-WS1TEST_GOOD
}

@test "ws_find with bad filesystem" {
    run ws_find --config bats/ws.conf -F DOESNOTEXIST
    assert_output --partial Error
    assert_failure
}

@test "ws_find no workspace" {
    run ws_find --config bats/ws.conf
    assert_failure
}

@test "ws_find bad config" {
    run ws_find --config bats/bad_ws.conf WS
    assert_output  --partial "Error"
    assert_failure
}

@test "ws_find bad option" {
    run ws_find --config bats/bad_ws.conf -T WS
    assert_output  --partial "Usage"
    assert_failure
}

cleanup() {
    ws_release --config bats/ws.conf $ws_name
}
