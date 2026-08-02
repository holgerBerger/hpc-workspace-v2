setup() {
    load 'test_helper/common-setup'
    _common_setup
}

@test "ws_restore with restorenosub carries over group workspace" {
    ws_name=group-restore-$RANDOM
    target_name=group-target-$RANDOM

    # create group workspace and data
    wsdir=$(ws_allocate --config bats/ws.conf_restorenosub -g -- $ws_name)
    echo "test data" > $wsdir/testfile
    assert_file_exists $wsdir/testfile

    # release the source workspace
    ws_release --config bats/ws.conf_restorenosub $ws_name

    # allocate target workspace
    target_dir=$(ws_allocate --config bats/ws.conf_restorenosub $target_name)

    # get the workspace ID to restore
    wsid=$(ws_restore --config bats/ws.conf_restorenosub -l | grep "$ws_name" | head -1)

    # restore to target
    run ws_restore_notest --config bats/ws.conf_restorenosub $wsid $target_name
    assert_success

    # verify data is at top level of target (not in subdirectory) since restorenosub=true
    assert_file_exists $target_dir/testfile
    assert_equal "test data" "$(cat $target_dir/testfile)"

    # verify the target DB entry has group info (set by carry-over)
    db_entry_path="/tmp/ws/ws2-db/${USER}-${target_name}"
    assert_file_exists $db_entry_path

    run grep "group:" $db_entry_path
    assert_success

    # cleanup
    ws_release --config bats/ws.conf_restorenosub $target_name
}
