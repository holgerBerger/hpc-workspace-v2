setup() {
    load 'test_helper/common-setup'
    ws_name="bats-workspace-test"
    _common_setup
}

@test "ws_restore present" {
    which ws_restore
}

@test "ws_restore print version" {
    run ws_restore --version
    assert_output --partial "ws_restore"
    assert_success
}

@test "ws_restore print help" {
    run ws_restore --help
    assert_output --partial "Usage"
    assert_success
}

@test "ws_restore list" {
    wsdir=$(ws_allocate  $ws_name)
    ws_release $ws_name
    run ws_restore  -l
    assert_output --partial $ws_name
    assert_success
}

@test "ws_restore workspace" {
    ws_name=setuid-restore-$RANDOM
    wsdir=$(ws_allocate  $ws_name)
    touch $wsdir/TESTFILE
    ws_release  $ws_name
    wsid=$( ws_restore -l | grep $ws_name | head -1)
    wsdir=$(ws_allocate $ws_name)
    ws_restore_notest $wsid $ws_name
    assert_file_exists $wsdir/$wsid/TESTFILE
}

@test "ws_restore dash-username workspace" {
    export WS_ALLOCATE=$(which ws_allocate)
    export WS_RELEASE=$(which ws_release)
    export WS_RESTORE=$(which ws_restore)
    export WS_FIND=$(which ws_find)

    ws_name=restore-dash-$RANDOM
    run sudo -u mean-user-name --preserve-env=ASAN_OPTIONS $WS_ALLOCATE -F ws1 $ws_name 5
    assert_success

    # Create data
    wsdir=$(sudo -u mean-user-name --preserve-env=ASAN_OPTIONS $WS_FIND -F ws1 $ws_name)
    echo "dash restore content" > "$wsdir"/restore_test.txt
    assert_file_exists "$wsdir"/restore_test.txt

    # Release
    run sudo -u mean-user-name --preserve-env=ASAN_OPTIONS $WS_RELEASE -F ws1 $ws_name
    assert_success

    # Get restore id
    wsid=$($WS_RESTORE -F ws1 -l | grep "$ws_name" | head -1)
    assert [ -n "$wsid" ]

    # Recreate target
    run sudo -u mean-user-name --preserve-env=ASAN_OPTIONS $WS_ALLOCATE -F ws1 $ws_name 5
    assert_success

    # Restore
    run sudo -u mean-user-name --preserve-env=ASAN_OPTIONS $WS_RESTORE -F ws1 $wsid $ws_name
    assert_success

    # Verify data
    wsdir=$(sudo -u mean-user-name --preserve-env=ASAN_OPTIONS $WS_FIND -F ws1 $ws_name)
    assert_file_exists "$wsdir/$wsid"/restore_test.txt
    run cat "$wsdir/$wsid"/restore_test.txt
    assert_output "dash restore content"

    # Cleanup
    run sudo -u mean-user-name --preserve-env=ASAN_OPTIONS $WS_RELEASE -F ws1 $ws_name
}

@test "ws_restore dash-username with delete-data" {
    export WS_ALLOCATE=$(which ws_allocate)
    export WS_RELEASE=$(which ws_release)
    export WS_RESTORE=$(which ws_restore)
    export WS_FIND=$(which ws_find)

    ws_name=restore-dash-del-$RANDOM
    run sudo -u mean-user-name --preserve-env=ASAN_OPTIONS $WS_ALLOCATE -F ws1 $ws_name 5
    assert_success

    wsdir=$(sudo -u mean-user-name --preserve-env=ASAN_OPTIONS $WS_FIND -F ws1 $ws_name)
    echo "will be deleted" > "$wsdir"/del_test.txt
    assert_file_exists "$wsdir"/del_test.txt

    run sudo -u mean-user-name --preserve-env=ASAN_OPTIONS $WS_RELEASE -F ws1 $ws_name
    assert_success

    wsid=$($WS_RESTORE -F ws1 -l | grep "$ws_name" | head -1)
    assert [ -n "$wsid" ]

    # Delete data via restore
    run sudo -u mean-user-name --preserve-env=ASAN_OPTIONS $WS_RESTORE -F ws1 --delete-data $wsid
    assert_success

    # Data should be gone from deleted directory
    refute_file_exists "$(dirname "$wsdir")/.removed/$wsid/del_test.txt"

    run sudo -u mean-user-name --preserve-env=ASAN_OPTIONS $WS_RELEASE -F ws1 $ws_name 2>/dev/null || true
}

@test "ws_restore list dash-username pattern match" {
    export WS_ALLOCATE=$(which ws_allocate)
    export WS_RELEASE=$(which ws_release)
    export WS_RESTORE=$(which ws_restore)

    ws_name=mean-user-list-$RANDOM
    run sudo -u mean-user-name --preserve-env=ASAN_OPTIONS $WS_ALLOCATE -F ws1 $ws_name 5
    assert_success
    run sudo -u mean-user-name --preserve-env=ASAN_OPTIONS $WS_RELEASE -F ws1 $ws_name
    assert_success

    # Pattern should match dash-prefixed names
    run sudo --preserve-env=ASAN_OPTIONS $WS_RESTORE -F ws1 -l "*mean-user-list*"
    assert_success
    assert_output --partial "mean-user-name-$ws_name"

    ws_release --config bats/ws.conf -F ws1 $ws_name 2>/dev/null || true
}
