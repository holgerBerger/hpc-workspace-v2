setup() {
    load 'test_helper/common-setup'
    _common_setup
}

@test "ws_allocate username with dash" {
        export WS_ALLOCATE=$(which ws_allocate)
        run sudo -u mean-user-name --preserve-env=ASAN_OPTIONS $WS_ALLOCATE -F ws1 TEST_MEAN
        assert_output --partial /tmp/ws/ws1/mean-user-name-TEST_MEAN
        assert_success
}

@test "ws_list username with dash" {
        export WS_LIST=$(which ws_list)
        run sudo -u mean-user-name --preserve-env=ASAN_OPTIONS $WS_LIST -F ws1 TEST_MEAN
        assert_output --partial "/tmp/ws/ws1/mean-user-name-TEST_MEAN"
        assert_success
}

@test "ws_release username with dash" {
        export WS_RELEASE=$(which ws_release)
        run sudo -u mean-user-name --preserve-env=ASAN_OPTIONS $WS_RELEASE -F ws1 TEST_MEAN
        assert_output --partial "workspace TEST_MEAN released"
        assert_success
}

@test "ws_expirer username with dash" {
        sudo rm -f /tmp/ws_expirer.log 2>/dev/null
        export WS_EXPIRER=$(which ws_expirer)
        run sudo --preserve-env=ASAN_OPTIONS $WS_EXPIRER -F ws1
        assert_output --regexp "keeping .*mean-user-name-TEST_MEAN"
        assert_success
        sudo rm -f /tmp/ws_expirer.log
}
