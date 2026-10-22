setup() {
    load 'test_helper/common-setup'
    _common_setup
    if [ ! -e ~/.ws_user.conf ]
    then
        echo "mail: $USER@localhost" > ~/.ws_user.conf
    fi
}


@test "ws_send_ical present" {
    which ws_send_ical
}

@test "ws_send_ical print help" {
    run ws_send_ical --help
    assert_output --partial "Usage"
    assert_output --partial "--username"
    assert_success
}

@test "ws_send_ical finds workspace on filesystem granted by user_acl" {
    # regression test: the filesystem ACL check must run against the calling user,
    # an empty username matches no user_acl entry and hid the whole filesystem
    conf=/tmp/ws_ical_acl.conf
    cat > "$conf" <<EOF
admins: [root]
adminmail: [root@a.com]
clustername: bats
dbgid: 2
dbuid: 2
duration: 10
maxextensions: 3
smtphost: localhost
mail_from: root@localhost
default: ws2
workspaces:
  ws1:
    database: /tmp/ws/ws1-db
    deleted: .removed
    keeptime: 7
    spaces: [/tmp/ws/ws1]
  ws2:
    database: /tmp/ws/ws2-db
    deleted: .removed
    user_acl: [$USER]
    keeptime: 7
    spaces: [/tmp/ws/ws2/1, /tmp/ws/ws2/2]
EOF
    run ws_allocate --config "$conf" -F ws2 ICALACL
    assert_success
    run ws_send_ical --config "$conf" -m $USER@localhost ICALACL
    # on a system without reachable SMTP this still shows the send attempt, the
    # bug produced "no workspace ICALACL found" instead
    refute_output --partial "no workspace"
    rm -f /tmp/ws/ws2-db/${USER}-ICALACL "$conf"
}

@test "ws_send_ical -u is rejected with a warning for non-admins" {
    # regression: -u must not let an unprivileged caller pick another identity for the
    # filesystem ACL check; the tool warns and stays on the calling user
    conf=/tmp/ws_ical_acl.conf
    cat > "$conf" <<EOF
admins: [root]
adminmail: [root@a.com]
clustername: bats
dbgid: 2
dbuid: 2
duration: 10
maxextensions: 3
smtphost: localhost
mail_from: root@localhost
default: ws2
workspaces:
  ws1:
    database: /tmp/ws/ws1-db
    deleted: .removed
    keeptime: 7
    spaces: [/tmp/ws/ws1]
  ws2:
    database: /tmp/ws/ws2-db
    deleted: .removed
    user_acl: [someoneelse]
    keeptime: 7
    spaces: [/tmp/ws/ws2/1, /tmp/ws/ws2/2]
EOF
    run ws_allocate --config "$conf" -F ws1 NOTME
    assert_success
    # even when naming the other user, the ACL check runs against the caller:
    # ws2 is only granted to someoneelse, so the workspace is not visible
    run ws_send_ical --config "$conf" -u someoneelse -m $USER@localhost NOTME
    assert_output --partial "choosing a user with -u is only allowed for root and admins"
    rm -f /tmp/ws/ws1-db/${USER}-NOTME "$conf"
}

@test "ws_send_ical no valid config file" {
    run ws_send_ical --config bats/bad_ws.conf -m $USER@localhost lala
    assert_output --partial "No adminmail in config!"
    assert_failure
}

@test "ws_send_ical invalid WORKSPACE" {
    run ws_send_ical --config bats/ws.conf -m $USER@localhost ICALTEST$$
    assert_output --partial "no workspace"
    assert_failure
}

@test "ws_send_ical valid WORKSPACE" {
    run ws_allocate --config bats/ws.conf ICALTEST
    assert_success
    run ws_send_ical --config bats/ws.conf -m $USER@localhost ICALTEST
    assert_output --partial "Calendar invitation sent"
    assert_success
}

@test "ws_send_ical missing workspace argument" {
    run ws_send_ical --config bats/ws.conf -m $USER@localhost
    assert_output --partial "no workspace name"
    assert_failure
}

@test "ws_send_ical missing mail argument without user config" {
    [ -f ~/.ws_user.conf ] && mv -f ~/.ws_user.conf ~/.ws_user.conf_testbackup
    ws_allocate --config bats/ws.conf NOMAIL
    run ws_send_ical --config bats/ws.conf NOMAIL
    assert_output --partial "without a mailadress"
    assert_failure
    ws_release --config bats/ws.conf NOMAIL
    [ -f ~/.ws_user.conf_testbackup ] && mv -f ~/.ws_user.conf_testbackup ~/.ws_user.conf
}

@test "ws_send_ical mail from user config" {
    [ -f ~/.ws_user.conf ] && mv -f ~/.ws_user.conf ~/.ws_user.conf_testbackup
    echo "mail: $USER@localhost" > ~/.ws_user.conf
    ws_allocate --config bats/ws.conf USERMAIL
    run ws_send_ical --config bats/ws.conf USERMAIL
    assert_output --partial "Took email address"
    assert_output --partial "Calendar invitation sent"
    assert_success
    ws_release --config bats/ws.conf USERMAIL
    rm -f ~/.ws_user.conf
    [ -f ~/.ws_user.conf_testbackup ] && mv -f ~/.ws_user.conf_testbackup ~/.ws_user.conf
}

@test "ws_send_ical invalid mail address" {
    ws_allocate --config bats/ws.conf INVALIDMAIL
    run ws_send_ical --config bats/ws.conf -m notvalid INVALIDMAIL
    assert_output --partial "Invalid email address"
    assert_failure
    ws_release --config bats/ws.conf INVALIDMAIL
}

@test "ws_send_ical invalid mail address no at sign" {
    ws_allocate --config bats/ws.conf NOAT
    run ws_send_ical --config bats/ws.conf -m noatsign.com NOAT
    assert_output --partial "Invalid email address"
    assert_failure
    ws_release --config bats/ws.conf NOAT
}

@test "ws_send_ical invalid workspace name with slash" {
    run ws_send_ical --config bats/ws.conf -m $USER@localhost invalid/name
    assert_output --partial "Illegal workspace name"
    assert_failure
}

@test "ws_send_ical invalid workspace name with special chars" {
    run ws_send_ical --config bats/ws.conf -m $USER@localhost "in$USER@name"
    assert_output --partial "Illegal workspace name"
    assert_failure
}

@test "ws_send_ical invalid workspace name with spaces" {
    run ws_send_ical --config bats/ws.conf -m $USER@localhost "invalid name"
    assert_output --partial "Illegal workspace name"
    assert_failure
}

@test "ws_send_ical valid workspace name with dash" {
    ws_allocate --config bats/ws.conf valid-dash
    run ws_send_ical --config bats/ws.conf -m $USER@localhost valid-dash
    assert_output --partial "Calendar invitation sent"
    assert_success
    ws_release --config bats/ws.conf valid-dash
}

@test "ws_send_ical valid workspace name with underscore" {
    ws_allocate --config bats/ws.conf valid_underscore
    run ws_send_ical --config bats/ws.conf -m $USER@localhost valid_underscore
    assert_output --partial "Calendar invitation sent"
    assert_success
    ws_release --config bats/ws.conf valid_underscore
}

@test "ws_send_ical valid workspace name with dot" {
    ws_allocate --config bats/ws.conf valid.dot
    run ws_send_ical --config bats/ws.conf -m $USER@localhost valid.dot
    assert_output --partial "Calendar invitation sent"
    assert_success
    ws_release --config bats/ws.conf valid.dot
}

@test "ws_send_ical with filesystem option" {
    ws_allocate --config bats/ws.conf -F ws1 WITHFS
    run ws_send_ical --config bats/ws.conf -F ws1 -m $USER@localhost WITHFS
    assert_output --partial "Calendar invitation sent"
    assert_success
    ws_release --config bats/ws.conf -F ws1 WITHFS
}

@test "ws_send_ical invalid filesystem" {
    ws_allocate --config bats/ws.conf BADFS
    run ws_send_ical --config bats/ws.conf -F invalidfs -m $USER@localhost BADFS
    assert_output --partial "invalid filesystem given"
    assert_failure
    ws_release --config bats/ws.conf BADFS
}

@test "ws_send_ical multiple workspaces requires filesystem" {
    ws_allocate --config bats/ws.conf -F ws1 MULTI
    ws_allocate --config bats/ws.conf -F ws2 MULTI
    run ws_send_ical --config bats/ws.conf -m $USER@localhost MULTI
    assert_output --partial "multiple workspaces found"
    assert_output --partial "use the -F option"
    assert_success
    ws_release --config bats/ws.conf -F ws1 MULTI
    ws_release --config bats/ws.conf -F ws2 MULTI
}

@test "ws_send_ical multiple workspaces with filesystem specified" {
    ws_allocate --config bats/ws.conf -F ws1 MULTIOK1
    ws_allocate --config bats/ws.conf -F ws2 MULTIOK1
    run ws_send_ical --config bats/ws.conf -F ws1 -m $USER@localhost MULTIOK1
    assert_output --partial "Calendar invitation sent"
    assert_success
    ws_release --config bats/ws.conf -F ws1 MULTIOK1
    ws_release --config bats/ws.conf -F ws2 MULTIOK1
}

@test "ws_send_ical short form filesystem option" {
    ws_allocate --config bats/ws.conf -F ws1 SHORTFS
    run ws_send_ical --config bats/ws.conf -F ws1 -m $USER@localhost SHORTFS
    assert_output --partial "Calendar invitation sent"
    assert_success
    ws_release --config bats/ws.conf -F ws1 SHORTFS
}

@test "ws_send_ical short form workspace option" {
    ws_allocate --config bats/ws.conf SHORTWS
    run ws_send_ical --config bats/ws.conf -n SHORTWS -m $USER@localhost
    assert_output --partial "Calendar invitation sent"
    assert_success
    ws_release --config bats/ws.conf SHORTWS
}

@test "ws_send_ical positional arguments" {
    ws_allocate --config bats/ws.conf POSITIONAL
    run ws_send_ical --config bats/ws.conf POSITIONAL $USER@localhost
    assert_output --partial "Calendar invitation sent"
    assert_success
    ws_release --config bats/ws.conf POSITIONAL
}

@test "ws_send_ical help shows usage" {
    run ws_send_ical --help
    assert_output --partial "workspace"
    assert_output --partial "mailadress"
    assert_output --partial "filesystem"
    assert_success
}

@test "ws_send_ical help describes purpose" {
    run ws_send_ical --help
    assert_output --partial "calendar invitation"
    assert_output --partial "expiration date"
    assert_success
}

@test "ws_send_ical config file option" {
    ws_allocate --config bats/ws.conf CONFOPT
    run ws_send_ical --config bats/ws.conf -m $USER@localhost CONFOPT
    assert_output --partial "Calendar invitation sent"
    assert_success
    ws_release --config bats/ws.conf CONFOPT
}

@test "ws_send_ical user config as symlink rejected" {
    skip "requires symlink creation in user home"
    [ -f ~/.ws_user.conf ] && mv -f ~/.ws_user.conf ~/.ws_user.conf_testbackup
    ln -s /tmp/fakefile ~/.ws_user.conf
    run ws_send_ical --config bats/ws.conf -m $USER@localhost SYMLINK
    assert_output --partial "can not be symlink"
    assert_failure
    rm -f ~/.ws_user.conf
    [ -f ~/.ws_user.conf_testbackup ] && mv -f ~/.ws_user.conf_testbackup ~/.ws_user.conf
}

@test "ws_send_ical workspace does not exist" {
    run ws_send_ical --config bats/ws.conf -m $USER@localhost NOTEXIST123
    assert_output --partial "no workspace"
    assert_failure
}

@test "ws_send_ical different workspaces on different filesystems" {
    ws_allocate --config bats/ws.conf -F ws1 DIFFWS1
    ws_allocate --config bats/ws.conf -F ws2 DIFFWS2

    run ws_send_ical --config bats/ws.conf -F ws1 -m $USER@localhost DIFFWS1
    assert_output --partial "Calendar invitation sent"
    assert_success

    run ws_send_ical --config bats/ws.conf -F ws2 -m $USER@localhost DIFFWS2
    assert_output --partial "Calendar invitation sent"
    assert_success

    ws_release --config bats/ws.conf -F ws1 DIFFWS1
    ws_release --config bats/ws.conf -F ws2 DIFFWS2
}

@test "ws_send_ical usage shown on missing arguments" {
    run ws_send_ical --config bats/ws.conf
    assert_output --partial "Usage"
    assert_failure
}

@test "ws_send_ical valid alphanumeric workspace name" {
    ws_allocate --config bats/ws.conf Test123
    run ws_send_ical --config bats/ws.conf -m $USER@localhost Test123
    assert_output --partial "Calendar invitation sent"
    assert_success
    ws_release --config bats/ws.conf Test123
}

@test "ws_send_ical invalid user config mail overridden by command line" {
    [ -f ~/.ws_user.conf ] && mv -f ~/.ws_user.conf ~/.ws_user.conf_testbackup
    echo "mail: invalid" > ~/.ws_user.conf
    ws_allocate --config bats/ws.conf OVERRIDE
    run ws_send_ical --config bats/ws.conf -m $USER@localhost OVERRIDE
    assert_output --partial "Calendar invitation sent"
    assert_success
    ws_release --config bats/ws.conf OVERRIDE
    rm -f ~/.ws_user.conf
    [ -f ~/.ws_user.conf_testbackup ] && mv -f ~/.ws_user.conf_testbackup ~/.ws_user.conf
}
