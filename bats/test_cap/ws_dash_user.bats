# Test suite for dash-username workspace handling under capability mode
# Tests workspace lifecycle for users like 'mean-user-name' and cross-group operations

setup() {
    load 'test_helper/common-setup'
    _common_setup
}

# ============================================================
# Dash-username workspace lifecycle (mean-user-name)
# ============================================================

@test "ws_extend dash-username workspace" {
    export WS_ALLOCATE=$(which ws_allocate)
    export WS_RELEASE=$(which ws_release)
    export WS_RESTORE=$(which ws_restore)
    export WS_LIST=$(which ws_list)
    
    # Create workspace as mean-user-name
    run sudo -u mean-user-name --preserve-env=ASAN_OPTIONS $WS_ALLOCATE -F ws1 DASHEXTEND 5
    assert_success
    assert_output --partial "creating workspace"

    # Extend as mean-user-name
    run sudo -u mean-user-name --preserve-env=ASAN_OPTIONS $WS_ALLOCATE -F ws1 -x DASHEXTEND 10
    assert_success
    assert_output --partial "extending workspace"

    # Verify extension applied
    run sudo -u mean-user-name --preserve-env=ASAN_OPTIONS $WS_LIST -F ws1 DASHEXTEND
    assert_success
    assert_output --partial "/tmp/ws/ws1/mean-user-name-DASHEXTEND"

    run sudo -u mean-user-name --preserve-env=ASAN_OPTIONS $WS_RELEASE -F ws1 DASHEXTEND
}

@test "ws_find dash-username workspace" {
    export WS_ALLOCATE=$(which ws_allocate)
    export WS_RELEASE=$(which ws_release)
    export WS_FIND=$(which ws_find)
    run sudo -u mean-user-name --preserve-env=ASAN_OPTIONS $WS_ALLOCATE -F ws1 DASHFIND 5
    assert_success

    run sudo -u mean-user-name --preserve-env=ASAN_OPTIONS $WS_FIND -F ws1 DASHFIND
    assert_success
    assert_output --partial /tmp/ws/ws1/mean-user-name-DASHFIND

    run sudo -u mean-user-name --preserve-env=ASAN_OPTIONS $WS_RELEASE -F ws1 DASHFIND
}

@test "ws_restore dash-username workspace" {
    export WS_ALLOCATE=$(which ws_allocate)
    export WS_RELEASE=$(which ws_release)
    export WS_RESTORE=$(which ws_restore_notest)
    export WS_LIST=$(which ws_list)
    export WS_FIND=$(which ws_find)

    ws_name=mean-user-dash-restore-$RANDOM
    run sudo -u mean-user-name --preserve-env=ASAN_OPTIONS $WS_ALLOCATE -G vagrant -F ws1 $ws_name 5
    assert_success

    # Create some data in the workspace
    wsdir=$(sudo -u mean-user-name --preserve-env=ASAN_OPTIONS $WS_FIND -F ws1 $ws_name)
    echo "dash-user data" > "$wsdir"/testfile.txt
    assert_file_exists "$wsdir"/testfile.txt

    # Release the workspace
    run sudo -u mean-user-name --preserve-env=ASAN_OPTIONS $WS_RELEASE -F ws1 $ws_name
    assert_success

    # Verify workspace is no longer accessible at original path
    assert_file_not_exist "$wsdir"

    # Find restored entry
    wsid=$(sudo -u mean-user-name --preserve-env=ASAN_OPTIONS $WS_RESTORE -F ws1 -l | grep "$ws_name" | head -1)
    [ -n "$wsid" ]

    # Recreate workspace to restore into
    run sudo -u mean-user-name --preserve-env=ASAN_OPTIONS $WS_ALLOCATE -G vagrant -F ws1 RESTORE_TARGET 5
    assert_success

    # Restore the data
    run sudo -u mean-user-name --preserve-env=ASAN_OPTIONS $WS_RESTORE --debug --trace -F ws1 $wsid RESTORE_TARGET
    assert_success

    # Verify data was restored
    wsdir=$(sudo -u mean-user-name --preserve-env=ASAN_OPTIONS $WS_FIND -F ws1 RESTORE_TARGET)
    assert_file_exists "$wsdir/$wsid"/testfile.txt
    run cat "$wsdir/$wsid"/testfile.txt
    assert_output "dash-user data"

    # Clean up
    run sudo -u mean-user-name --preserve-env=ASAN_OPTIONS $WS_RELEASE -F ws1 RESTORE_TARGET
}

@test "ws_restore list dash-username works with pattern" {
    export WS_ALLOCATE=$(which ws_allocate)
    export WS_RELEASE=$(which ws_release)
    export WS_RESTORE=$(which ws_restore)

    ws_name=mean-user-dash-list-$RANDOM
    run sudo -u mean-user-name --preserve-env=ASAN_OPTIONS $WS_ALLOCATE -F ws1 $ws_name 5
    assert_success

    # Release and check list works
    run sudo -u mean-user-name --preserve-env=ASAN_OPTIONS $WS_RELEASE -F ws1 $ws_name
    assert_success

    run sudo --preserve-env=ASAN_OPTIONS $WS_RESTORE -F ws1 -l "*dash-list*"
    assert_success
    assert_output --partial "mean-user-name-$ws_name"

    run sudo -u mean-user-name --preserve-env=ASAN_OPTIONS $WS_ALLOCATE -F ws1 $ws_name 5
    run sudo -u mean-user-name --preserve-env=ASAN_OPTIONS $WS_RELEASE -F ws1 $ws_name
}

# ============================================================
# Dash in workspace ID itself (not just username)
# ============================================================

@test "ws_allocate multi-dash workspace ID by mean-user-name" {
    export WS_ALLOCATE=$(which ws_allocate)
    run sudo -u mean-user-name --preserve-env=ASAN_OPTIONS $WS_ALLOCATE -F ws1 multi-dash-id-test 5
    assert_success
    assert_output --partial /tmp/ws/ws1/mean-user-name-multi-dash-id-test

    run sudo -u mean-user-name --preserve-env=ASAN_OPTIONS $WS_RELEASE -F ws1 multi-dash-id-test
}

@test "ws_allocate dots in workspace ID by mean-user-name" {
    export WS_ALLOCATE=$(which ws_allocate)
    run sudo -u mean-user-name --preserve-env=ASAN_OPTIONS $WS_ALLOCATE -F ws1 dots.in.name 5
    assert_success

    run sudo -u mean-user-name --preserve-env=ASAN_OPTIONS $WS_RELEASE -F ws1 dots.in.name
}

@test "ws_allocate single character workspace ID by mean-user-name" {
    export WS_ALLOCATE=$(which ws_allocate)
    run sudo -u mean-user-name --preserve-env=ASAN_OPTIONS $WS_ALLOCATE -F ws1 x 5
    assert_success

    run sudo -u mean-user-name --preserve-env=ASAN_OPTIONS $WS_RELEASE -F ws1 x
}

@test "ws_allocate alphanumeric and dash workspace ID by mean-user-name" {
    export WS_ALLOCATE=$(which ws_allocate)
    run sudo -u mean-user-name --preserve-env=ASAN_OPTIONS $WS_ALLOCATE -F ws1 alpha-numeric-ws-123 5
    assert_success
    assert_output --partial /tmp/ws/ws1/mean-user-name-alpha-numeric-ws-123

    run sudo -u mean-user-name --preserve-env=ASAN_OPTIONS $WS_RELEASE -F ws1 alpha-numeric-ws-123
}

# ============================================================
# Group workspace creation (mean-user-name belongs to vagrant, usera, userb)
# ============================================================

@test "ws_allocate writable group workspace by mean-user-name" {
    export WS_ALLOCATE=$(which ws_allocate)
    export WS_RELEASE=$(which ws_release)
    export WS_RESTORE=$(which ws_restore)
    export WS_LIST=$(which ws_list)
    export WS_FIND=$(which ws_find)

    # mean-user-name is in group vagrant (set in rh_create_users.sh)
    run sudo -u mean-user-name --preserve-env=ASAN_OPTIONS $WS_ALLOCATE -G vagrant GROUPWS-GRPPERM 5
    assert_success

    # Verify group ownership and sticky bit via stat
    wsdir=$(sudo -u mean-user-name --preserve-env=ASAN_OPTIONS $WS_FIND -g GROUPWS-GRPPERM)
    run stat -c "%A %G" "$wsdir"
    assert_output --regexp "drwxrws--- vagrant"

    # workspace should be in group workspace directory
    assert_dir_exists "$wsdir"

    run sudo -u mean-user-name --preserve-env=ASAN_OPTIONS $WS_RELEASE GROUPWS-GRPPERM
}

@test "ws_allocate readable group workspace by mean-user-name" {
    export WS_ALLOCATE=$(which ws_allocate)
    export WS_RELEASE=$(which ws_release)
    export WS_RESTORE=$(which ws_restore)
    export WS_LIST=$(which ws_list)
    export WS_FIND=$(which ws_find)

    run sudo -u mean-user-name --preserve-env=ASAN_OPTIONS $WS_ALLOCATE -g -- GROUPWS-READABLE 5
    assert_success

    wsdir=$(sudo -u mean-user-name --preserve-env=ASAN_OPTIONS $WS_FIND -u mean-user-name -g GROUPWS-READABLE)
    run stat -c "%A %G" "$wsdir"
    # Readable group gets drwxr-s---
    assert_output --partial "drwxr-s---"

    run sudo -u mean-user-name --preserve-env=ASAN_OPTIONS $WS_RELEASE GROUPWS-READABLE
}

@test "ws_allocate writable group by userb to usera group" {
    export WS_ALLOCATE=$(which ws_allocate)
    export WS_RELEASE=$(which ws_release)
    export WS_RESTORE=$(which ws_restore)
    export WS_LIST=$(which ws_list)
    export WS_FIND=$(which ws_find)

    # userb is member of usera (set in rh_create_users.sh)
    run sudo -u userb --preserve-env=ASAN_OPTIONS $WS_ALLOCATE -F ws1 -G usera GROUPWS-USERB 5
    assert_success

    wsdir=$(sudo -u userb --preserve-env=ASAN_OPTIONS $WS_FIND -F ws1 GROUPWS-USERB)
    run stat -c "%G" "$wsdir"
    assert_output --partial "usera"

    run sudo -u userb --preserve-env=ASAN_OPTIONS $WS_RELEASE -F ws1 GROUPWS-USERB
}

@test "ws_list group workspaces visible to userb from usera" {
    export WS_ALLOCATE=$(which ws_allocate)
    export WS_RELEASE=$(which ws_release)
    export WS_RESTORE=$(which ws_restore)
    export WS_LIST=$(which ws_list)

    # Mean-user-name creates a group workspace for usera
    run sudo -u mean-user-name --preserve-env=ASAN_OPTIONS $WS_ALLOCATE -F ws1 -G usera GROUPWS-VISIBLE 5
    assert_success

    # userb should see it (userb is in usera group)
    run sudo -u userb --preserve-env=ASAN_OPTIONS $WS_LIST -g -F ws1 GROUPWS-VISIBLE
    assert_success
    assert_output --partial "/tmp/ws/ws1/mean-user-name-GROUPWS-VISIBLE"

    run sudo -u mean-user-name --preserve-env=ASAN_OPTIONS $WS_RELEASE -F ws1 GROUPWS-VISIBLE
}

@test "ws_list group workspaces not visible to unrelated user" {
    # vagrant is in group vagrant, usera, userb
    # usera should not see vagrant's group workspace if usera is only in vagrant group
    export WS_ALLOCATE=$(which ws_allocate)
    export WS_LIST=$(which ws_list)
    export WS_RELEASE=$(which ws_release)

    # vagrant creates a workspace in vagrant group
    run sudo -u vagrant --preserve-env=ASAN_OPTIONS $WS_ALLOCATE -F ws1 -G vagrant GROUPWS-VAGRANT 5
    assert_success

    # Create as regular (non-group) user - usera is in vagrant group too, so actually it would be visible
    # Let's test that userb can see vagrant's workspace since both are in vagrant group
    run sudo -u userb --preserve-env=ASAN_OPTIONS $WS_LIST -g -F ws1 GROUPWS-VAGRANT
    assert_success

    run sudo -u vagrant --preserve-env=ASAN_OPTIONS $WS_RELEASE -F ws1 GROUPWS-VAGRANT
}

# ============================================================
# Cross-user extend operations
# ============================================================

@test "userb can extend vagrant's writable group workspace" {
    export WS_ALLOCATE=$(which ws_allocate)
    export WS_RELEASE=$(which ws_release)

    vagrant_dir=$(sudo -u vagrant --preserve-env=ASAN_OPTIONS $(which ws_allocate) -F ws1 -G vagrant CROSS-EXTEND 5)
    assert [ -n "$vagrant_dir" ]

    # userb (in vagrant group) extends
    run sudo -u userb --preserve-env=ASAN_OPTIONS $WS_ALLOCATE -F ws1 -x -u vagrant CROSS-EXTEND 10
    assert_success
    assert_output --partial "extending workspace"

    run sudo -u vagrant --preserve-env=ASAN_OPTIONS $WS_RELEASE -F ws1 CROSS-EXTEND
}

@test "userb cannot extend non-group workspace" {
    export WS_ALLOCATE=$(which ws_allocate)
    export WS_RELEASE=$(which ws_release)

    # vagrant creates a personal (non-group) workspace
    vagrant_dir=$(sudo -u vagrant --preserve-env=ASAN_OPTIONS $(which ws_allocate) -F ws1 NOMEMBER-WS 5)
    assert [ -n "$vagrant_dir" ]

    # userb tries to extend - should fail with permission error
    run sudo -u userb --preserve-env=ASAN_OPTIONS $WS_ALLOCATE -F ws1 -x -u vagrant NOMEMBER-WS 10
    assert_failure
    assert_output --partial "not owner"

    run sudo -u vagrant --preserve-env=ASAN_OPTIONS $WS_RELEASE -F ws1 NOMEMBER-WS
}

@test "vagrant can extend userb's writable group workspace" {
    export WS_ALLOCATE=$(which ws_allocate)
    export WS_RELEASE=$(which ws_release)

    # userb creates writable workspace for vagrant group
    run sudo -u userb --preserve-env=ASAN_OPTIONS $WS_ALLOCATE -F ws1 -G vagrant VAGRANT-EXTEND 5
    assert_success

    # vagrant (in vagrant group) extends
    run sudo -u vagrant --preserve-env=ASAN_OPTIONS $WS_ALLOCATE -F ws1 -x -u userb VAGRANT-EXTEND 10
    assert_success
    assert_output --partial "extending workspace"

    run sudo -u userb --preserve-env=ASAN_OPTIONS $WS_RELEASE -F ws1 VAGRANT-EXTEND
}

# ============================================================
# Dash-username comment and mail modifications
# ============================================================

@test "ws_allocate -x change comment for dash-username workspace" {
    export WS_ALLOCATE=$(which ws_allocate)
    export WS_LIST=$(which ws_list)
    export WS_RELEASE=$(which ws_release)



    run sudo -u mean-user-name --preserve-env=ASAN_OPTIONS $WS_ALLOCATE -F ws1 DASHCOMMENT 5
    assert_success

    run sudo -u mean-user-name --preserve-env=ASAN_OPTIONS $WS_ALLOCATE -F ws1 -x -c "dash changed" DASHCOMMENT
    assert_success
    assert_output --partial "changed comment"

    run sudo -u mean-user-name --preserve-env=ASAN_OPTIONS $WS_LIST -F ws1 DASHCOMMENT
    assert_output --partial "dash changed"

    run sudo -u mean-user-name --preserve-env=ASAN_OPTIONS $WS_RELEASE -F ws1 DASHCOMMENT
}

@test "ws_allocate -x change mail for dash-username workspace" {
    export WS_ALLOCATE=$(which ws_allocate)
    export WS_LIST=$(which ws_list)
    export WS_RELEASE=$(which ws_release)

    run sudo -u mean-user-name --preserve-env=ASAN_OPTIONS $WS_ALLOCATE -F ws1 DASHMAIL 5
    assert_success

    run sudo -u mean-user-name --preserve-env=ASAN_OPTIONS $WS_ALLOCATE -F ws1 -x -m "dash@test.com" DASHMAIL
    assert_success
    assert_output --partial "changed mail"

    run sudo -u mean-user-name --preserve-env=ASAN_OPTIONS $WS_LIST -v -F ws1 DASHMAIL
    assert_output --partial "dash@test.com"

    run sudo -u mean-user-name --preserve-env=ASAN_OPTIONS $WS_RELEASE -F ws1 DASHMAIL
}

# ============================================================
# Dash-username extending by group members
# ============================================================

@test "vagrant extends mean-user-name group workspace (usera)" {
    export WS_ALLOCATE=$(which ws_allocate)
    export WS_RELEASE=$(which ws_release)

    # mean-user-name creates writable for usera group
    run sudo -u mean-user-name --preserve-env=ASAN_OPTIONS $WS_ALLOCATE -F ws1 -G usera MEAN-GROUP-WORKSPACE 5
    assert_success

    # vagrant is in usera group, can extend
    run sudo -u vagrant --preserve-env=ASAN_OPTIONS $WS_ALLOCATE -F ws1 -x -u mean-user-name MEAN-GROUP-WORKSPACE 10
    assert_success
    assert_output --partial "extending workspace"

    run sudo -u mean-user-name --preserve-env=ASAN_OPTIONS $WS_RELEASE -F ws1 MEAN-GROUP-WORKSPACE
}

# ============================================================
# Dash-username filesystem selection
# ============================================================

@test "ws_expirer handles dash-username workspace" {
    sudo rm -f /tmp/ws_expirer.log 2>/dev/null
    export WS_EXPIRER=$(which ws_expirer)
    export WS_ALLOCATE=$(which ws_allocate)
    export WS_RELEASE=$(which ws_release)

    sudo -u mean-user-name --preserve-env=ASAN_OPTIONS $WS_ALLOCATE -F ws1 EXPIRER-DASH 5
    run sudo --preserve-env=ASAN_OPTIONS $WS_EXPIRER -F ws1
    assert_success
    assert_output --regexp "found valid.*mean-user-name-EXPIRER-DASH"
    sudo rm -f /tmp/ws_expirer.log
    sudo -u mean-user-name --preserve-env=ASAN_OPTIONS $WS_RELEASE -F ws1 EXPIRER-DASH
}

@test "ws_allocate -x extension count persists for dash-username" {
    export WS_ALLOCATE=$(which ws_allocate)
    export WS_RELEASE=$(which ws_release)

    run sudo -u mean-user-name --preserve-env=ASAN_OPTIONS $WS_ALLOCATE -F ws1 EXTCOUNT-DASH 5
    assert_success

    # First extension (remaining: 2)
    run sudo -u mean-user-name --preserve-env=ASAN_OPTIONS $WS_ALLOCATE -F ws1 -x EXTCOUNT-DASH 10
    assert_success
    assert_output --partial "remaining extensions  : 2"

    # Second extension (remaining: 1)
    run sudo -u mean-user-name --preserve-env=ASAN_OPTIONS $WS_ALLOCATE -F ws1 -x EXTCOUNT-DASH 15
    assert_success
    assert_output --partial "remaining extensions  : 1"

    # Third extension (remaining: 0)
    run sudo -u mean-user-name --preserve-env=ASAN_OPTIONS $WS_ALLOCATE -F ws1 -x EXTCOUNT-DASH 20
    assert_success
    assert_output --partial "remaining extensions  : 0"

    # Fourth should fail
    run sudo -u mean-user-name --preserve-env=ASAN_OPTIONS $WS_ALLOCATE -F ws1 -x EXTCOUNT-DASH 25
    assert_failure
    assert_output --partial "no more extensions!"

    run sudo -u mean-user-name --preserve-env=ASAN_OPTIONS $WS_RELEASE -F ws1 EXTCOUNT-DASH
}

@test "ws_allocate -x zero duration for dash-username (no extension)" {
    export WS_ALLOCATE=$(which ws_allocate)
    export WS_RELEASE=$(which ws_release)

    run sudo -u mean-user-name --preserve-env=ASAN_OPTIONS $WS_ALLOCATE -F ws1 ZEROEXT-DASH 5
    assert_success

    # Change comment without extension (duration not given)
    run sudo -u mean-user-name --preserve-env=ASAN_OPTIONS $WS_ALLOCATE -F ws1 -x -c "no extend" ZEROEXT-DASH
    assert_success
    assert_output --partial "changed comment"

    run sudo -u mean-user-name --preserve-env=ASAN_OPTIONS $WS_RELEASE -F ws1 ZEROEXT-DASH
}
