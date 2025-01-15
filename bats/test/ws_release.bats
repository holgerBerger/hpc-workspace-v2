setup() {
    load 'test_helper/common-setup'
    _common_setup
    ws_name="bats_workspace_test"
    ws_name2="bats_workspace_test2"
    export ws_name
}

@test "ws_release present" {
    which ws_release
}

@test "ws_release print version" {
    run ws_release --version
    assert_output --partial "workspace"
}

@test "ws_release print help" {
    run ws_release --help
    assert_output --partial "Usage"
}

@test "ws_release releases directory" {
    wsdir=$(ws_allocate --config bats/ws.conf -F ws1 $ws_name)
    assert_dir_exist $wsdir
    ws_release --config bats/ws.conf  -F ws1 $ws_name
    assert_dir_not_exist $wsdir
}

@test "ws_release delete directory with data" {
    wsdir=$(ws_allocate --config bats/ws.conf -F ws1  $ws_name2)
    assert_dir_exist $wsdir
    mkdir $wsdir/DATA
    touch $wsdir/DATA/file
    ws_release --config bats/ws.conf --delete-data -F ws1 $ws_name2
    assert_dir_not_exist $wsdir
}

cleanup() {
    ws_release $ws_name
}
