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
    wsdir=$(ws_allocate --config bats/ws.conf $ws_name)
    ws_release --config bats/ws.conf $ws_name
    run ws_restore --config bats/ws.conf -l
    assert_output --partial $ws_name
    assert_success
}

@test "ws_restore list with pattern" {
    wsdir=$(ws_allocate --config bats/ws.conf $ws_name)
    ws_allocate --config bats/ws.conf DONTSHOW
    ws_release --config bats/ws.conf $ws_name
    ws_release --config bats/ws.conf DONTSHOW
    run ws_restore --config bats/ws.conf -l "bats*"
    assert_output --partial $ws_name
    refute_output --partial DONTSHOW
    assert_success
    run ws_restore --config bats/ws.conf -l
    assert_output --partial DONTSHOW
    assert_output --partial $ws_name
    assert_success
    run ws_restore --config bats/ws.conf -l "*SHOW*"
    assert_output --partial DONTSHOW
}

@test "ws_restore workspace" {
    ws_name=restore-$RANDOM
    wsdir=$(ws_allocate --config bats/ws.conf $ws_name)
    touch $wsdir/TESTFILE
    ws_release --config bats/ws.conf $ws_name
    wsid=$( ws_restore --config bats/ws.conf -l | grep $ws_name | head -1)
    wsdir=$(ws_allocate --config bats/ws.conf $ws_name)
    ws_restore_notest --config bats/ws.conf $wsid $ws_name
    assert_file_exists $wsdir/$wsid/TESTFILE
}

@test "ws_restore delete-data" {
    ws_name=restore-$RANDOM
    wsdir=$(ws_allocate --config bats/ws.conf -F ws1 $ws_name)
    touch $wsdir/TESTFILE
    ws_release --config bats/ws.conf -F ws1 $ws_name
    wsid=$( ws_restore --config bats/ws.conf -F ws1 -l | grep $ws_name | head -1)
    assert_file_exists /tmp/ws/ws1/.removed/$wsid/TESTFILE
    ws_restore_notest --config bats/ws.conf --delete-data $wsid
    assert_file_not_exists /tmp/ws/ws1/.removed/$wsid/TESTFILE
}

@test "ws_restore workspace with username and - in name" {
    ws_name=$USER-restore
    run ws_release --config bats/ws.conf $ws_name
    wsdir=$(ws_allocate --config bats/ws.conf $ws_name)
    touch $wsdir/TESTFILE
    run ws_release --config bats/ws.conf $ws_name
    assert_output --partial "released"
    assert_success
    wsid=$(ws_restore --config bats/ws.conf -l | grep $ws_name | head -1)
    wsdir=$(ws_allocate --config bats/ws.conf $ws_name)
    run ws_restore_notest --config bats/ws.conf $wsid $ws_name
    assert_success
    assert_file_exists $wsdir/$wsid/TESTFILE
}

@test "ws_restore list with brief flag" {
    ws_name=brief-test-$RANDOM
    wsdir=$(ws_allocate --config bats/ws.conf $ws_name)
    ws_release --config bats/ws.conf $ws_name
    run ws_restore --config bats/ws.conf -l -b "$ws_name*"
    assert_output --partial $ws_name
    refute_output --partial "unavailable since"
    refute_output --partial "restorable until"
    assert_success
}

@test "ws_restore list shows timestamps" {
    ws_name=timestamp-test-$RANDOM
    wsdir=$(ws_allocate --config bats/ws.conf $ws_name)
    ws_release --config bats/ws.conf $ws_name
    run ws_restore --config bats/ws.conf -l "$ws_name*"
    assert_output --partial $ws_name
    assert_output --partial "unavailable since"
    assert_output --partial "restorable until"
    assert_output --partial "in filesystem"
    assert_success
}

@test "ws_restore list on specific filesystem" {
    ws_name=fs-list-$RANDOM
    wsdir=$(ws_allocate --config bats/ws.conf -F ws1 $ws_name)
    ws_release --config bats/ws.conf -F ws1 $ws_name
    run ws_restore --config bats/ws.conf -F ws1 -l "$ws_name*"
    assert_output --partial $ws_name
    assert_success
}

@test "ws_restore list with invalid filesystem" {
    run ws_restore --config bats/ws.conf -F invalidfs -l
    assert_output --partial "invalid filesystem given"
    assert_success
}

@test "ws_restore list empty result" {
    run ws_restore --config bats/ws.conf -l "nonexistent-pattern-$RANDOM"
    refute_output --partial "unavailable since"
    assert_success
}

@test "ws_restore without target argument" {
    ws_name=notarget-$RANDOM
    wsdir=$(ws_allocate --config bats/ws.conf $ws_name)
    ws_release --config bats/ws.conf $ws_name
    wsid=$(ws_restore --config bats/ws.conf -l | grep $ws_name | head -1)
    run ws_restore_notest --config bats/ws.conf $wsid
    assert_output --partial "no target given"
    assert_failure
}

@test "ws_restore non-existent workspace" {
    ws_name=target-for-nonexist-$RANDOM
    wsdir=$(ws_allocate --config bats/ws.conf $ws_name)
    run ws_restore_notest --config bats/ws.conf fake-workspace-id-12345 $ws_name
    assert_output --partial "invalid workspace name"
    assert_success
    ws_release --config bats/ws.conf $ws_name
}

@test "ws_restore to non-existent target" {
    ws_name=source-ws-$RANDOM
    wsdir=$(ws_allocate --config bats/ws.conf $ws_name)
    ws_release --config bats/ws.conf $ws_name
    wsid=$(ws_restore --config bats/ws.conf -l | grep $ws_name | head -1)
    run ws_restore_notest --config bats/ws.conf $wsid nonexistent-target-$RANDOM
    assert_output --partial "target does not exist"
    assert_success
}

@test "ws_restore invalid workspace name with slash" {
    run ws_restore_notest --config bats/ws.conf "invalid/name" sometarget
    assert_output --partial "Illegal workspace name"
    assert_failure
}

@test "ws_restore invalid workspace name with special chars" {
    run ws_restore_notest --config bats/ws.conf "invalid@name" sometarget
    assert_output --partial "Illegal workspace name"
    assert_failure
}

@test "ws_restore invalid target name with slash" {
    # L3 regression: the target builds the DB entry ID "<user>-<target>" and must
    # pass the same charset validation as the workspace name
    run ws_restore_notest --config bats/ws.conf validname '../evil'
    assert_output --partial "Illegal workspace name"
    assert_failure
    run ws_restore_notest --config bats/ws.conf validname -t '../evil'
    assert_output --partial "Illegal workspace name"
    assert_failure
}

@test "ws_restore invalid target name with special chars" {
    run ws_restore_notest --config bats/ws.conf validname 'bad;name'
    assert_output --partial "Illegal workspace name"
    assert_failure
}

@test "ws_restore target with legal special chars passes validation" {
    # the new check must not be over-broad: dash, dot and underscore stay allowed;
    # any failure must come from the source lookup, not from target validation
    run ws_restore_notest --config bats/ws.conf nosuchws 'my.target_1-2'
    refute_output --partial "Illegal workspace name"
}

@test "ws_restore pattern matching with asterisk" {
    ws_name1=pattern-match-a-$RANDOM
    ws_name2=pattern-match-b-$RANDOM
    ws_allocate --config bats/ws.conf $ws_name1
    ws_allocate --config bats/ws.conf $ws_name2
    ws_release --config bats/ws.conf $ws_name1
    ws_release --config bats/ws.conf $ws_name2
    run ws_restore --config bats/ws.conf -l "pattern-match-*"
    assert_output --partial $ws_name1
    assert_output --partial $ws_name2
    assert_success
}

@test "ws_restore pattern matching single character" {
    ws_name1=match-a
    ws_name2=match-b
    ws_allocate --config bats/ws.conf $ws_name1
    ws_allocate --config bats/ws.conf $ws_name2
    ws_release --config bats/ws.conf $ws_name1
    ws_release --config bats/ws.conf $ws_name2
    run ws_restore --config bats/ws.conf -l "match-?-*"
    assert_output --partial $ws_name1
    assert_output --partial $ws_name2
    assert_success
}

@test "ws_restore with filesystem option restores correctly" {
    ws_name=fs-restore-$RANDOM
    target_name=fs-target-$RANDOM
    wsdir=$(ws_allocate --config bats/ws.conf -F ws1 $ws_name)
    echo "test data" > $wsdir/datafile
    ws_release --config bats/ws.conf -F ws1 $ws_name
    wsid=$(ws_restore --config bats/ws.conf -F ws1 -l | grep $ws_name | head -1)
    target_dir=$(ws_allocate --config bats/ws.conf -F ws1 $target_name)
    ws_restore_notest --config bats/ws.conf -F ws1 $wsid $target_name
    assert_file_exists $target_dir/$wsid/datafile
    assert_equal "$(cat $target_dir/$wsid/datafile)" "test data"
    ws_release --config bats/ws.conf -F ws1 $target_name
}

@test "ws_restore preserves file content" {
    ws_name=content-test-$RANDOM
    target_name=content-target-$RANDOM
    wsdir=$(ws_allocate --config bats/ws.conf $ws_name)
    echo "important content" > $wsdir/important.txt
    mkdir -p $wsdir/subdir
    echo "nested content" > $wsdir/subdir/nested.txt
    ws_release --config bats/ws.conf $ws_name
    wsid=$(ws_restore --config bats/ws.conf -l | grep $ws_name | head -1)
    target_dir=$(ws_allocate --config bats/ws.conf $target_name)
    ws_restore_notest --config bats/ws.conf $wsid $target_name
    assert_file_exists $target_dir/$wsid/important.txt
    assert_file_exists $target_dir/$wsid/subdir/nested.txt
    assert_equal "$(cat $target_dir/$wsid/important.txt)" "important content"
    assert_equal "$(cat $target_dir/$wsid/subdir/nested.txt)" "nested content"
    ws_release --config bats/ws.conf $target_name
}

@test "ws_restore removed from list after restoration" {
    ws_name=removal-test-$RANDOM
    target_name=removal-target-$RANDOM
    wsdir=$(ws_allocate --config bats/ws.conf $ws_name)
    ws_release --config bats/ws.conf $ws_name
    wsid=$(ws_restore --config bats/ws.conf -l | grep $ws_name | head -1)
    target_dir=$(ws_allocate --config bats/ws.conf $target_name)
    ws_restore_notest --config bats/ws.conf $wsid $target_name
    run ws_restore --config bats/ws.conf -l
    refute_output --partial "$wsid"
    assert_success
    ws_release --config bats/ws.conf $target_name
}

@test "ws_restore delete-data removes from list" {
    ws_name=delete-list-$RANDOM
    wsdir=$(ws_allocate --config bats/ws.conf $ws_name)
    ws_release --config bats/ws.conf $ws_name
    wsid=$(ws_restore --config bats/ws.conf -l | grep $ws_name | head -1)
    ws_restore_notest --config bats/ws.conf --delete-data $wsid
    run ws_restore --config bats/ws.conf -l
    refute_output --partial "$wsid"
    assert_success
}

@test "ws_restore list shows correct filesystem" {
    ws_name=fs-display-$RANDOM
    wsdir=$(ws_allocate --config bats/ws.conf -F ws1 $ws_name)
    ws_release --config bats/ws.conf -F ws1 $ws_name
    run ws_restore --config bats/ws.conf -l "$ws_name*"
    assert_output --partial "in filesystem"
    assert_output --partial "ws1"
    assert_success
}

@test "ws_restore short form list option" {
    ws_name=shortlist-$RANDOM
    wsdir=$(ws_allocate --config bats/ws.conf $ws_name)
    ws_release --config bats/ws.conf $ws_name
    run ws_restore --config bats/ws.conf -l "$ws_name*"
    assert_output --partial $ws_name
    assert_success
}

@test "ws_restore short form brief option" {
    ws_name=shortbrief-$RANDOM
    wsdir=$(ws_allocate --config bats/ws.conf $ws_name)
    ws_release --config bats/ws.conf $ws_name
    run ws_restore --config bats/ws.conf -b -l "$ws_name*"
    assert_output --partial $ws_name
    refute_output --partial "unavailable since"
    assert_success
}

@test "ws_restore workspace with dots in name" {
    ws_name=restore.with.dots
    target_name=target.with.dots
    wsdir=$(ws_allocate --config bats/ws.conf $ws_name)
    ws_release --config bats/ws.conf $ws_name
    wsid=$(ws_restore --config bats/ws.conf -l | grep "$ws_name" | head -1)
    target_dir=$(ws_allocate --config bats/ws.conf $target_name)
    run ws_restore_notest --config bats/ws.conf $wsid $target_name
    assert_success
    ws_release --config bats/ws.conf $target_name
}

@test "ws_restore workspace with underscores in name" {
    ws_name=restore_with_underscores
    target_name=target_with_underscores
    wsdir=$(ws_allocate --config bats/ws.conf $ws_name)
    ws_release --config bats/ws.conf $ws_name
    wsid=$(ws_restore --config bats/ws.conf -l | grep "$ws_name" | head -1)
    target_dir=$(ws_allocate --config bats/ws.conf $target_name)
    run ws_restore_notest --config bats/ws.conf $wsid $target_name
    assert_success
    ws_release --config bats/ws.conf $target_name
}

@test "ws_restore list all without pattern" {
    ws_name1=listall-a-$RANDOM
    ws_name2=listall-b-$RANDOM
    ws_allocate --config bats/ws.conf $ws_name1
    ws_allocate --config bats/ws.conf $ws_name2
    ws_release --config bats/ws.conf $ws_name1
    ws_release --config bats/ws.conf $ws_name2
    run ws_restore --config bats/ws.conf -l
    assert_output --partial $ws_name1
    assert_output --partial $ws_name2
    assert_success
}

@test "ws_restore help shows attention message" {
    run ws_restore --config bats/ws.conf --help
    assert_output --partial "attention"
    assert_output --partial "workspace_name argument"
    assert_success
}

@test "ws_restore missing both workspace and list option" {
    run ws_restore_notest --config bats/ws.conf
    assert_output --partial "neither workspace nor -l specified"
    assert_failure
}

@test "ws_restore detects duplicate workspace IDs across filesystems" {
    ws_name=duplicate-test-$RANDOM
    target_name=duplicate-target-$RANDOM
    wsdir1=$(ws_allocate --config bats/ws.conf -F ws1 $ws_name)
    wsdir2=$(ws_allocate --config bats/ws.conf -F ws2 $ws_name)
    ws_release --config bats/ws.conf -F ws1 $ws_name
    ws_release --config bats/ws.conf -F ws2 $ws_name
    wsid=$(ws_restore --config bats/ws.conf -l | grep $ws_name | head -1)
    target_dir=$(ws_allocate --config bats/ws.conf $target_name)
    run ws_restore_notest --config bats/ws.conf $wsid $target_name
    assert_output --partial "not unique"
    assert_output --partial "please give filesystem with -F"
    assert_failure
    ws_release --config bats/ws.conf $target_name
}

@test "ws_restore uses filesystem option to resolve duplicate workspace IDs" {
    ws_name=fs-resolve-test-$RANDOM
    target_name=fs-resolve-target-$RANDOM
    wsdir1=$(ws_allocate --config bats/ws.conf -F ws1 $ws_name)
    echo "data on ws1" > $wsdir1/datafile
    wsdir2=$(ws_allocate --config bats/ws.conf -F ws2 $ws_name)
    echo "data on ws2" > $wsdir2/datafile
    ws_release --config bats/ws.conf -F ws1 $ws_name
    ws_release --config bats/ws.conf -F ws2 $ws_name
    wsid=$(ws_restore --config bats/ws.conf -F ws1 -l | grep $ws_name | head -1)
    target_dir=$(ws_allocate --config bats/ws.conf -F ws1 $target_name)
    ws_restore_notest --config bats/ws.conf -F ws1 $wsid $target_name
    assert_file_exists $target_dir/$wsid/datafile
    assert_equal "$(cat $target_dir/$wsid/datafile)" "data on ws1"
    ws_release --config bats/ws.conf -F ws1 $target_name
}

@test "ws_restore ruh check cannot be bypassed via argv[0]" {
    # L2 regression: the human check used to be skipped whenever
    # basename(argv[0]) != "ws_restore" - trivially spoofed with "exec -a".
    # The name is now taken from /proc/self/exe, so even under a spoofed
    # argv[0] the ruh test must run: with empty stdin it fails and nothing
    # is restored.
    local src_name=ruh-src-$RANDOM
    local target_name=ruh-target-$RANDOM
    local target_dir
    target_dir=$(ws_allocate --config bats/ws.conf $target_name)
    local wsdir
    wsdir=$(ws_allocate --config bats/ws.conf $src_name)
    echo "ruh data" > "$wsdir/TESTFILE"
    ws_release --config bats/ws.conf $src_name

    local wsid
    wsid=$(ws_restore --config bats/ws.conf -l | grep $src_name | head -1)
    assert [ -n "$wsid" ]

    run bash -c 'exec -a ws_restore_notest ws_restore --config bats/ws.conf "$1" "$2" </dev/null' bash "$wsid" "$target_name"

    # the prompt appeared => ruh test ran despite the spoofed argv[0]
    assert_output --partial "Animal"
    # ... and failed on EOF => entry still restorable, data untouched
    # (note: -l takes a name pattern matched as user-pattern, so filter the
    # full list for the unique wsid instead of passing it as pattern arg)
    run ws_restore --config bats/ws.conf -l
    assert_output --partial "$wsid"
    assert_file_not_exists "$target_dir/$wsid/TESTFILE"

    # control: the real test-copy binary still skips the human check
    run ws_restore_notest --config bats/ws.conf "$wsid" "$target_name"
    assert_success
    assert_file_exists "$target_dir/$wsid/TESTFILE"

    ws_release --config bats/ws.conf $target_name
}
