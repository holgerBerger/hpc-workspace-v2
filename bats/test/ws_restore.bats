setup() {
    load 'test_helper/common-setup'
    _common_setup
}

@test "ws_restore present" {
    which ws_restore
}

@test "ws_restore print version" {
    run ws_restore --version
    assert_output --partial "workspace"
    assert_success
}

@test "ws_restore print help" {
    run ws_restore --help
    assert_output --partial "Usage"
    assert_success
}
