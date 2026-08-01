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
    assert_output --partial "ws_release"
    assert_success
}

@test "ws_release print help" {
    run ws_release --help
    assert_output --partial "Usage"
    assert_success
}

@test "ws_release releases directory" {
    wsdir=$(ws_allocate  -F ws1 $ws_name)
    assert_dir_exists $wsdir
    ws_release  -F ws1 $ws_name
    assert_dir_not_exists $wsdir
}

@test "ws_release delete directory with data" {
    wsdir=$(ws_allocate -F ws1  $ws_name2)
    assert_dir_exists $wsdir
    mkdir $wsdir/DATA
    touch $wsdir/DATA/file
    ws_release --delete-data -F ws1 $ws_name2
    assert_dir_not_exists $wsdir
}

@test "ws_release dash-username workspace" {
    export WS_ALLOCATE=$(which ws_allocate)
    export WS_RELEASE=$(which ws_release)
    run sudo -u mean-user-name --preserve-env=ASAN_OPTIONS $WS_ALLOCATE -F ws1 DASH-RELEASE 5
    assert_success

    run sudo -u mean-user-name --preserve-env=ASAN_OPTIONS $WS_RELEASE -F ws1 DASH-RELEASE
    assert_success
    assert_output --partial "workspace DASH-RELEASE released"
}

@test "ws_release group workspace by non-group-member fails" {
    # vagrant creates a private workspace for usera group
    export WS_ALLOCATE=$(which ws_allocate)
    export WS_RELEASE=$(which ws_release)

    vagrant_dir=$(sudo -u vagrant --preserve-env=ASAN_OPTIONS $WS_ALLOCATE -F ws1 -G vagrant GRP-WS-PERM 5)

    # userb tries to release - should fail because it's vagrant's workspace
    run sudo -u userb --preserve-env=ASAN_OPTIONS $WS_RELEASE -F ws1 GRP-WS-PERM
    assert_failure
    assert_output --partial "Non-existent workspace given"

    # vagrant releases successfully
    sudo -u vagrant --preserve-env=ASAN_OPTIONS $WS_RELEASE -F ws1 GRP-WS-PERM
}

@test "ws_release workspace with dots in name" {
    export WS_ALLOCATE=$(which ws_allocate)
    export WS_RELEASE=$(which ws_release)
    run sudo -u mean-user-name --preserve-env=ASAN_OPTIONS $WS_ALLOCATE -F ws1 dots.in.ws 5
    assert_success

    run sudo -u mean-user-name --preserve-env=ASAN_OPTIONS $WS_RELEASE -F ws1 dots.in.ws
    assert_success

    # Clean up any remaining entries
    ws_release --force dots.in.ws 2>/dev/null || true
}

@test "ws_release workspace with underscores in name" {
    export WS_ALLOCATE=$(which ws_allocate)
    export WS_RELEASE=$(which ws_release)
    run sudo -u mean-user-name --preserve-env=ASAN_OPTIONS $WS_ALLOCATE -F ws1 underscores_in_ws 5
    assert_success

    run sudo -u mean-user-name --preserve-env=ASAN_OPTIONS $WS_RELEASE -F ws1 underscores_in_ws
    assert_success

    ws_release --force underscores_in_ws 2>/dev/null || true
}

cleanup() {
    ws_release $ws_name
}
