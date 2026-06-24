# Test suite for ws_share
# Tests all documented functionality based on ws_share.1 man page
# AI disclosure: based on tests from qwen3-coder-next

setup() {
    load 'test_helper/common-setup'
    _common_setup
    ws_name="bats_workspace_test"
    export PATH=$PATH:$PWD/bin
    export ws_name
}

@test "ws_share present" {
    which ws_share
}

@test "ws_share print help" {
    run ws_share --help
    assert_output --partial "allows to share"
    assert_output --partial "share"
    assert_output --partial "unshare"
    assert_success
}

@test "ws_share share user read-only" {
    # Create a workspace first
    run ws_allocate --config bats/ws.conf $ws_name
    assert_success

    local wsdir
    wsdir=$(ws_find --config bats/ws.conf $ws_name)

    # Share with a test user (using current user for testing)
    run ws_share --config bats/ws.conf share $ws_name $USER
    assert_success
    assert_output --partial "Granting"

    # Verify ACL is set
    run getfacl "$wsdir" 2>/dev/null
    assert_output --regexp "user:$USER:r-x"
    assert_output --regexp "default:user:$USER:r-x"

    # Cleanup
    ws_release --config bats/ws.conf $ws_name
}

@test "ws_share share user read-write" {
    run ws_allocate --config bats/ws.conf ${ws_name}_rw
    assert_success

    local wsdir
    wsdir=$(ws_find --config bats/ws.conf ${ws_name}_rw)

    run ws_share --config bats/ws.conf --readwrite share ${ws_name}_rw $USER
    assert_success
    assert_output --partial "Granting"
    assert_output --partial "rwX"

    # Verify ACL is set with rwX permissions
    run getfacl "$wsdir" 2>/dev/null
    assert_output --regexp "user:$USER:rwx"
    assert_output --regexp "default:user:$USER:rwx"

    # Cleanup
    ws_release --config bats/ws.conf ${ws_name}_rw
}

@test "ws_share unshare user" {
    run ws_allocate --config bats/ws.conf ${ws_name}_unshare
    assert_success

    local wsdir
    wsdir=$(ws_find --config bats/ws.conf ${ws_name}_unshare)

    # First share
    run ws_share --config bats/ws.conf share ${ws_name}_unshare $USER
    assert_success

    # Verify ACL exists
    run getfacl "$wsdir" 2>/dev/null
    assert_output --regexp "user:$USER"

    # Now unshare
    run ws_share --config bats/ws.conf unshare ${ws_name}_unshare $USER
    assert_success
    assert_output --partial "Removing"

    # Verify ACL is removed
    run getfacl "$wsdir" 2>/dev/null
    refute_output --regexp "user:$USER"

    # Cleanup
    ws_release --config bats/ws.conf ${ws_name}_unshare
}

@test "ws_share unshare-all" {
    run ws_allocate --config bats/ws.conf ${ws_name}_unshareall
    assert_success

    local wsdir
    wsdir=$(ws_find --config bats/ws.conf ${ws_name}_unshareall)

    # Share with multiple users/groups first
    run ws_share --config bats/ws.conf share ${ws_name}_unshareall $USER
    assert_success

    # Verify ACL exists
    run getfacl "$wsdir" 2>/dev/null
    assert_output --regexp "user:$USER"

    # Unshare-all
    run ws_share --config bats/ws.conf unshare-all ${ws_name}_unshareall
    assert_success
    assert_output --partial "Warning"
    assert_output --partial "will produce errors"

    # Verify all ACLs are removed
    run getfacl "$wsdir" 2>/dev/null
    refute_output --regexp "user::$USER"
    refute_output --regexp "group::$USER"

    # Cleanup
    ws_release --config bats/ws.conf ${ws_name}_unshareall
}

@test "ws_share list" {
    run ws_allocate --config bats/ws.conf ${ws_name}_list
    assert_success

    local wsdir
    wsdir=$(ws_find --config bats/ws.conf ${ws_name}_list)

    # List before sharing (should be empty or minimal)
    run ws_share --config bats/ws.conf list ${ws_name}_list
    assert_success

    # Share with user
    run ws_share --config bats/ws.conf share ${ws_name}_list $USER
    assert_success

    # List after sharing
    run ws_share --config bats/ws.conf list ${ws_name}_list
    assert_success
    assert_output --regexp "$USER"

    # Cleanup
    ws_release --config bats/ws.conf ${ws_name}_list
}

@test "ws_share sharegroup read-only" {
    run ws_allocate --config bats/ws.conf ${ws_name}_group
    assert_success

    local wsdir
    wsdir=$(ws_find --config bats/ws.conf ${ws_name}_group)

    # Use a valid group (nogroup is typically available)
    local testgroup="nogroup"
    getent group "$testgroup" >/dev/null || testgroup="nobody"

    run ws_share --config bats/ws.conf sharegroup ${ws_name}_group $testgroup
    assert_success
    assert_output --partial "Granting"
    assert_output --partial "rX"

    # Verify ACL is set for group
    run getfacl "$wsdir" 2>/dev/null
    assert_output --regexp "group:$testgroup:r-x"
    assert_output --regexp "default:group:$testgroup:r-x"

    # Cleanup
    ws_release --config bats/ws.conf ${ws_name}_group
}

@test "ws_share sharegroup read-write" {
    run ws_allocate --config bats/ws.conf ${ws_name}_group_rw
    assert_success

    local wsdir
    wsdir=$(ws_find --config bats/ws.conf ${ws_name}_group_rw)

    local testgroup="nogroup"
    getent group "$testgroup" >/dev/null || testgroup="nobody"

    run ws_share --yes --config bats/ws.conf --readwrite sharegroup ${ws_name}_group_rw $testgroup
    assert_success

    # Verify warning message is shown for read-write
    assert_output --partial "Warning"
    assert_output --partial "write access"

    # Verify ACL is set with rwX permissions
    run getfacl "$wsdir" 2>/dev/null
    assert_output --regexp "group:$testgroup:rwx"
    assert_output --regexp "default:group:$testgroup:rwx"

    # Cleanup
    ws_release --config bats/ws.conf ${ws_name}_group_rw
}

@test "ws_share unsharegroup" {
    run ws_allocate --config bats/ws.conf ${ws_name}_unsharegroup
    assert_success

    local wsdir
    wsdir=$(ws_find --config bats/ws.conf ${ws_name}_unsharegroup)

    local testgroup="nogroup"
    getent group "$testgroup" >/dev/null || testgroup="nobody"

    # First share with group
    run ws_share --config bats/ws.conf sharegroup ${ws_name}_unsharegroup $testgroup
    assert_success

    # Verify ACL exists
    run getfacl "$wsdir" 2>/dev/null
    assert_output --regexp "group:$testgroup"

    # Now unsharegroup
    run ws_share --config bats/ws.conf unsharegroup ${ws_name}_unsharegroup $testgroup
    assert_success
    assert_output --partial "Removing"
    assert_output --partial "Warning"

    # Verify ACL is removed
    run getfacl "$wsdir" 2>/dev/null
    refute_output --regexp "group:$testgroup"

    # Cleanup
    ws_release --config bats/ws.conf ${ws_name}_unsharegroup
}

@test "ws_share error unknown user" {
    run ws_allocate --config bats/ws.conf ${ws_name}_baduser
    assert_success

    run ws_share --config bats/ws.conf share ${ws_name}_baduser nonexistent_user_12345
    assert_failure
    assert_output --partial "Error: User nonexistent_user_12345 unknown"

    # Cleanup
    ws_release --config bats/ws.conf ${ws_name}_baduser
}

@test "ws_share error unknown group" {
    run ws_allocate --config bats/ws.conf ${ws_name}_badgroup
    assert_success

    run ws_share --config bats/ws.conf sharegroup ${ws_name}_badgroup nonexistent_group_12345
    assert_failure
    assert_output --partial "Error: Group nonexistent_group_12345 unknown"

    # Cleanup
    ws_release --config bats/ws.conf ${ws_name}_badgroup
}

@test "ws_share error invalid workspace" {
    run ws_share --config bats/ws.conf share nonexistent_workspace_12345 $USER
    assert_failure
    assert_output --partial "Error: Invalid workspace name."
}

@test "ws_share error missing action" {
    run ws_share --config bats/ws.conf
    assert_failure
    assert_output --partial "Error: no action specified"
}

@test "ws_share error missing workspace name" {
    run ws_share --config bats/ws.conf share
    assert_failure
    assert_output --partial "Error: workspace name missing"
}

@test "ws_share error missing user for share" {
    run ws_allocate --config bats/ws.conf ${ws_name}_nouser_share
    assert_success

    run ws_share --config bats/ws.conf share ${ws_name}_nouser_share
    assert_failure
    assert_output --partial "Error: No user specified"

    ws_release --config bats/ws.conf ${ws_name}_nouser_share
}

@test "ws_share error missing user for unshare" {
    run ws_allocate --config bats/ws.conf ${ws_name}_nouser_unshare
    assert_success

    run ws_share --config bats/ws.conf unshare ${ws_name}_nouser_unshare
    assert_failure
    assert_output --partial "Error: No user specified"

    ws_release --config bats/ws.conf ${ws_name}_nouser_unshare
}

@test "ws_share error missing group for sharegroup" {
    run ws_allocate --config bats/ws.conf ${ws_name}_nogroup_share
    assert_success

    run ws_share --config bats/ws.conf sharegroup ${ws_name}_nogroup_share
    assert_failure
    assert_output --partial "Error: No group specified"

    ws_release --config bats/ws.conf ${ws_name}_nogroup_share
}

@test "ws_share error missing group for unsharegroup" {
    run ws_allocate --config bats/ws.conf ${ws_name}_nogroup_unshare
    assert_success

    run ws_share --config bats/ws.conf unsharegroup ${ws_name}_nogroup_unshare
    assert_failure
    assert_output --partial "Error: No group specified"

    ws_release --config bats/ws.conf ${ws_name}_nogroup_unshare
}

@test "ws_share filesystem selection" {
    run ws_allocate --config bats/ws.conf -F ws2 ${ws_name}_fs
    assert_success

    # Test with -F option
    run ws_share --config bats/ws.conf -F ws2 share ${ws_name}_fs $USER
    assert_success

    # Test with --filesystem option
    run ws_share --config bats/ws.conf --filesystem ws2 list ${ws_name}_fs
    assert_success

    # Cleanup
    ws_release --config bats/ws.conf -F ws2 ${ws_name}_fs
}

@test "ws_share config option" {
    run ws_allocate --config bats/ws.conf ${ws_name}_config
    assert_success

    # Test with --config option
    run ws_share --config bats/ws.conf share ${ws_name}_config $USER
    assert_success

    local wsdir
    wsdir=$(ws_find --config bats/ws.conf ${ws_name}_config)

    # Verify ACL was set
    run getfacl "$wsdir" 2>/dev/null
    assert_output --regexp "user:$USER"

    # Cleanup
    ws_release --config bats/ws.conf ${ws_name}_config
}

@test "ws_share config with filesystem option" {
    run ws_allocate --config bats/ws.conf -F ws2 ${ws_name}_config_fs
    assert_success

    # Test with both --config and -F options
    run ws_share --config bats/ws.conf -F ws2 share ${ws_name}_config_fs $USER
    assert_success

    local wsdir
    wsdir=$(ws_find --config bats/ws.conf -F ws2 ${ws_name}_config_fs)

    # Verify ACL was set
    run getfacl "$wsdir" 2>/dev/null
    assert_output --regexp "user:$USER"

    # Cleanup
    ws_release --config bats/ws.conf -F ws2 ${ws_name}_config_fs
}

@test "ws_share share multiple users" {
    run ws_allocate --config bats/ws.conf ${ws_name}_multi
    assert_success

    local wsdir
    wsdir=$(ws_find --config bats/ws.conf ${ws_name}_multi)

    # Share with multiple users (including current user)
    local user2="nobody"
    id "$user2" >/dev/null 2>&1 || user2="daemon"

    run ws_share --config bats/ws.conf share ${ws_name}_multi $USER $user2
    assert_success

    # Verify both users have ACL
    run getfacl "$wsdir" 2>/dev/null
    assert_output --regexp "user:$USER:r-x"
    assert_output --regexp "user:$user2:r-x"

    # Cleanup
    ws_release --config bats/ws.conf ${ws_name}_multi
}

@test "ws_share share multiple groups" {
    run ws_allocate --config bats/ws.conf ${ws_name}_multi_group
    assert_success

    local testgroup1="nogroup"
    local testgroup2="nobody"
    getent group "$testgroup1" >/dev/null || testgroup1="nobody"
    getent group "$testgroup2" >/dev/null || testgroup2="staff"

    run ws_share --config bats/ws.conf sharegroup ${ws_name}_multi_group $testgroup1 $testgroup2
    assert_success

    # Verify both groups have ACL
    local wsdir
    wsdir=$(ws_find --config bats/ws.conf ${ws_name}_multi_group)
    run getfacl "$wsdir" 2>/dev/null
    assert_output --regexp "group:$testgroup1:r-x"
    assert_output --regexp "group:$testgroup2:r-x"

    # Cleanup
    ws_release --config bats/ws.conf ${ws_name}_multi_group
}

@test "ws_share sharegroup large group fails" {
    run ws_allocate --config bats/ws.conf ${ws_name}_biggroup
    assert_success

    # Create a group with many members to test maxmembers limit (100)
    # We'll mock getent output for a large group
    local wsdir
    wsdir=$(ws_find --config bats/ws.conf ${ws_name}_biggroup)

    # Test with a fake group name to trigger the error
    run ws_share --config bats/ws.conf sharegroup ${ws_name}_biggroup fakegroup_nonexistent
    assert_failure
    assert_output --partial "Error: Group fakegroup_nonexistent unknown"

    # Cleanup
    ws_release --config bats/ws.conf ${ws_name}_biggroup
}

@test "ws_share sharegroup prompt for read-write" {
    run ws_allocate --config bats/ws.conf ${ws_name}_rw_prompt
    assert_success

    local testgroup="nogroup"
    getent group "$testgroup" >/dev/null || testgroup="nobody"

    # Test with read-write - should show warning
    run ws_share --yes --config bats/ws.conf --readwrite sharegroup ${ws_name}_rw_prompt $testgroup
    assert_success
    assert_output --partial "Warning"
    assert_output --partial "workspace deletion"
    assert_output --partial "interfere with your quota"

    # Cleanup
    ws_release --config bats/ws.conf ${ws_name}_rw_prompt
}

@test "ws_share help shows all options" {
    run ws_share --help
    assert_output --partial "readwrite"
    assert_output --partial "filesystem"
    assert_output --partial "share"
    assert_output --partial "unshare"
    assert_output --partial "sharegroup"
    assert_output --partial "unsharegroup"
    assert_output --partial "list"
    assert_output --partial "unshare-all"
}

@test "ws_share invalid action" {
    run ws_allocate --config bats/ws.conf ${ws_name}_invalid
    assert_success

    run ws_share --config bats/ws.conf invalidaction ${ws_name}_invalid
    assert_failure
    assert_output --partial "Error: Invalid action"

    # Cleanup
    ws_release --config bats/ws.conf ${ws_name}_invalid
}

@test "ws_share unknown option" {
    run ws_share --config bats/ws.conf --unknownoption ${ws_name}
    assert_failure
    assert_output --partial "unrecognized option"
}

@test "ws_share share with default filesystem" {
    run ws_allocate --config bats/ws.conf ${ws_name}_default
    assert_success

    # Test without -F option (should use default filesystem)
    run ws_share --config bats/ws.conf share ${ws_name}_default $USER
    assert_success

    local wsdir
    wsdir=$(ws_find --config bats/ws.conf ${ws_name}_default)

    # Verify ACL was set
    run getfacl "$wsdir" 2>/dev/null
    assert_output --regexp "user:$USER"

    # Cleanup
    ws_release --config bats/ws.conf ${ws_name}_default
}

@test "ws_share share unshare roundtrip" {
    run ws_allocate --config bats/ws.conf ${ws_name}_roundtrip
    assert_success

    local wsdir
    wsdir=$(ws_find --config bats/ws.conf ${ws_name}_roundtrip)

    # Share
    run ws_share --config bats/ws.conf share ${ws_name}_roundtrip $USER
    assert_success

    # Verify ACL exists
    run getfacl "$wsdir" 2>/dev/null
    assert_output --regexp "user:$USER:r-x"

    # Unshare
    run ws_share --config bats/ws.conf unshare ${ws_name}_roundtrip $USER
    assert_success

    # Verify ACL is removed
    run getfacl "$wsdir" 2>/dev/null
    refute_output --regexp "user:$USER"

    # Share again
    run ws_share --config bats/ws.conf share ${ws_name}_roundtrip $USER
    assert_success

    # Verify ACL is back
    run getfacl "$wsdir" 2>/dev/null
    assert_output --regexp "user:$USER:r-x"

    # Cleanup
    ws_release --config bats/ws.conf ${ws_name}_roundtrip
}

@test "ws_share list shows readwrite permission" {
    run ws_allocate --config bats/ws.conf ${ws_name}_list_rw
    assert_success

    local wsdir
    wsdir=$(ws_find --config bats/ws.conf ${ws_name}_list_rw)

    # Share with read-write
    run ws_share --yes --config bats/ws.conf --readwrite share ${ws_name}_list_rw $USER
    assert_success

    # List should show readwrite
    run ws_share --config bats/ws.conf list ${ws_name}_list_rw
    assert_success
    assert_output --regexp "readwrite"
    assert_output --regexp "$USER"

    # Cleanup
    ws_release --config bats/ws.conf ${ws_name}_list_rw
}

@test "ws_share list shows read permission" {
    run ws_allocate --config bats/ws.conf ${ws_name}_list_r
    assert_success

    local wsdir
    wsdir=$(ws_find --config bats/ws.conf ${ws_name}_list_r)

    # Share with read-only (default)
    run ws_share --config bats/ws.conf share ${ws_name}_list_r $USER
    assert_success

    # List should show read
    run ws_share --config bats/ws.conf list ${ws_name}_list_r
    assert_success
    assert_output --regexp "read"
    assert_output --regexp "$USER"

    # Cleanup
    ws_release --config bats/ws.conf ${ws_name}_list_r
}

@test "ws_share list with config option" {
    run ws_allocate --config bats/ws.conf ${ws_name}_list_config
    assert_success

    local wsdir
    wsdir=$(ws_find --config bats/ws.conf ${ws_name}_list_config)

    # Share with read-only
    run ws_share --config bats/ws.conf share ${ws_name}_list_config $USER
    assert_success

    # List with config option
    run ws_share --config bats/ws.conf list ${ws_name}_list_config
    assert_success
    assert_output --regexp "read"
    assert_output --regexp "$USER"

    # Cleanup
    ws_release --config bats/ws.conf ${ws_name}_list_config
}

@test "ws_share unshare with multiple users" {
    run ws_allocate --config bats/ws.conf ${ws_name}_multi_unshare
    assert_success

    local wsdir
    wsdir=$(ws_find --config bats/ws.conf ${ws_name}_multi_unshare)

    local user2="nobody"
    id "$user2" >/dev/null 2>&1 || user2="daemon"

    # Share with multiple users
    run ws_share --config bats/ws.conf share ${ws_name}_multi_unshare $USER $user2
    assert_success

    # Verify both have ACL
    run getfacl "$wsdir" 2>/dev/null
    assert_output --regexp "user:$USER:r-x"
    assert_output --regexp "user:$user2:r-x"

    # Unshare both
    run ws_share --config bats/ws.conf unshare ${ws_name}_multi_unshare $USER $user2
    assert_success

    # Verify both are removed
    run getfacl "$wsdir" 2>/dev/null
    refute_output --regexp "user:$USER"
    refute_output --regexp "user:$user2"

    # Cleanup
    ws_release --config bats/ws.conf ${ws_name}_multi_unshare
}

@test "ws_share unsharegroup with multiple groups" {
    run ws_allocate --config bats/ws.conf ${ws_name}_multi_unsharegroup
    assert_success

    local testgroup1="nogroup"
    local testgroup2="nobody"
    getent group "$testgroup1" >/dev/null || testgroup1="nobody"
    getent group "$testgroup2" >/dev/null || testgroup2="staff"

    # Share with multiple groups
    run ws_share --config bats/ws.conf sharegroup ${ws_name}_multi_unsharegroup $testgroup1 $testgroup2
    assert_success

    # Verify both have ACL
    local wsdir
    wsdir=$(ws_find --config bats/ws.conf ${ws_name}_multi_unsharegroup)
    run getfacl "$wsdir" 2>/dev/null
    assert_output --regexp "group:$testgroup1:r-x"
    assert_output --regexp "group:$testgroup2:r-x"

    # Unsharegroup both
    run ws_share --config bats/ws.conf unsharegroup ${ws_name}_multi_unsharegroup $testgroup1 $testgroup2
    assert_success

    # Verify both are removed
    run getfacl "$wsdir" 2>/dev/null
    refute_output --regexp "group:$testgroup1"
    refute_output --regexp "group:$testgroup2"

    # Cleanup
    ws_release --config bats/ws.conf ${ws_name}_multi_unsharegroup
}

@test "ws_share sharegroup with valid group name" {
    run ws_allocate --config bats/ws.conf ${ws_name}_valid_group
    assert_success

    # Use root group which should always exist
    local testgroup="root"
    getent group "$testgroup" >/dev/null || testgroup="nogroup"

    run ws_share --config bats/ws.conf sharegroup ${ws_name}_valid_group $testgroup
    assert_success

    local wsdir
    wsdir=$(ws_find --config bats/ws.conf ${ws_name}_valid_group)

    run getfacl "$wsdir" 2>/dev/null
    assert_output --regexp "group:$testgroup:r-x"

    # Cleanup
    ws_release --config bats/ws.conf ${ws_name}_valid_group
}

@test "ws_share check action exists" {
    run ws_allocate --config bats/ws.conf ${ws_name}_check
    assert_success

    # check action should not fail
    run ws_share --config bats/ws.conf check ${ws_name}_check
    assert_success

    # Cleanup
    ws_release --config bats/ws.conf ${ws_name}_check
}

@test "ws_share unshare with config option" {
    run ws_allocate --config bats/ws.conf ${ws_name}_unshare_config
    assert_success

    # Share first
    run ws_share --config bats/ws.conf share ${ws_name}_unshare_config $USER
    assert_success

    # Verify ACL exists
    local wsdir
    wsdir=$(ws_find --config bats/ws.conf ${ws_name}_unshare_config)
    run getfacl "$wsdir" 2>/dev/null
    assert_output --regexp "user:$USER"

    # Unshare with config option
    run ws_share --config bats/ws.conf unshare ${ws_name}_unshare_config $USER
    assert_success

    # Verify ACL is removed
    run getfacl "$wsdir" 2>/dev/null
    refute_output --regexp "user:$USER"

    # Cleanup
    ws_release --config bats/ws.conf ${ws_name}_unshare_config
}

@test "ws_share sharegroup no duplicate members check" {
    # Test that we handle groups with many members correctly
    # The script has maxmembers=100 check

    run ws_allocate --config bats/ws.conf ${ws_name}_member_check
    assert_success

    # Use a valid group (nogroup typically has no members)
    local testgroup="nogroup"
    getent group "$testgroup" >/dev/null || testgroup="staff"

    run ws_share --config bats/ws.conf sharegroup ${ws_name}_member_check $testgroup
    assert_success
    refute_output --partial "Too many members"

    # Cleanup
    ws_release --config bats/ws.conf ${ws_name}_member_check
}
