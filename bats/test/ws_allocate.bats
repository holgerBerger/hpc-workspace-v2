setup() {
    load 'test_helper/common-setup'
    _common_setup
    ws_name="bats_workspace_test"
    export ws_name
}

@test "ws_allocate present" {
    which ws_allocate
}

@test "ws_allocate print version" {
    run ws_allocate --version
    assert_output --partial "ws_allocate"
    assert_success
}

@test "ws_allocate print help" {
    run ws_allocate --help
    assert_output --partial "Usage"
    assert_success
}

@test "ws_allocate creates directory" {
    wsdir=$(ws_allocate --config bats/ws.conf $ws_name)
    assert_dir_exists $wsdir
}

@test "ws_allocate rejects dangerous workspace names" {
    # prevent trying to level up directories
    run ws_allocate --config bats/ws.conf '../noup'
    assert_failure
    run ws_allocate --config bats/ws.conf 'no/../up'
    assert_failure
    run ws_allocate --config bats/ws.conf 'noup/..'
    assert_failure

    # forbid slashes in names, e.g., potential risk for absolute paths
    run ws_allocate --config bats/ws.conf 'no/slashes'
    assert_failure

    # forbid snake in names, e.g., potential risk for home path access
    run ws_allocate --config bats/ws.conf 'no~home'
    assert_failure

    # prevent any malicious command injections in bash scripts
    run ws_allocate --config bats/ws.conf 'no;semicolons'
    assert_failure
    run ws_allocate --config bats/ws.conf 'no`semicolons'
    assert_failure
    run ws_allocate --config bats/ws.conf 'no#comments'
    assert_failure
    run ws_allocate --config bats/ws.conf 'no$dollars'
    assert_failure

    # potentially dangerous as well in bash scripts, e.g., globbing
    run ws_allocate --config bats/ws.conf 'no?questions'
    assert_failure
    run ws_allocate --config bats/ws.conf 'no*stars'
    assert_failure
    run ws_allocate --config bats/ws.conf 'no:colons'
    assert_failure
    run ws_allocate --config bats/ws.conf 'no,commas'
    assert_failure

    # other things we do not want in workspace names
    run ws_allocate --config bats/ws.conf '_StartingWithUnderscoreDisallowed'
    assert_failure
}

@test "ws_allocate warn about missing adminmail in config" {
    run ws_allocate --config bats/bad_ws.conf TEST
    assert_output  --partial "warning: No adminmail in config!"
    assert_success
}

@test "ws_allocate bad config, no workspaces" {
    run ws_allocate --config bats/no_ws_ws.conf TEST
    assert_output  --partial "no valid filesystems"
    assert_failure
}

@test "ws_allocate not alloctable" {
    run ws_allocate --config bats/permissions_ws.conf -F ws1 TEST
    assert_output  --partial "not be used for allocation"
    assert_failure
}

@test "ws_allocate not extendable" {
    run ws_allocate --config bats/ws.conf -F ws1 TEST_EXTEND 4
    assert_success
    run ws_allocate --config bats/permissions_ws.conf -F ws1 -x TEST_EXTEND 8
    assert_output  --partial "can not be extended"
    assert_failure
}


@test "ws_allocate bad option" {
    run ws_allocate --config bats/bad_ws.conf --doesnotexist WS
    assert_output  --partial "Usage"
    assert_failure
}

@test "ws_allocate no option" {
    run ws_allocate --config bats/bad_ws.conf
    assert_output  --partial "Usage"
    assert_failure
}

@test "ws_allocate invalid name" {
    run ws_allocate --config bats/ws.conf INVALID/NAME
    assert_output  --partial "Illegal workspace name"
    assert_failure
}

@test "ws_allocate too long duration (allocation and extension)" {
    run ws_allocate --config bats/ws.conf TOLONG 1000
    assert_output  --partial "Duration longer than allowed"
    assert_success
    run ws_allocate --config bats/ws.conf -x TOLONG 1000
    assert_success
    assert_output  --partial "Duration longer than allowed"
    rm -f /tmp/ws/ws2-db/${USER}-TOLONG
}

@test "ws_allocate without duration" {
    [ -f ~/.ws_user.conf ] && mv -f ~/.ws_user.conf ~/.ws_user.conf_testbackup
    run ws_allocate --config bats/ws.conf NODURATION
    assert_success
    assert_output --partial "remaining time in days: 30"
    [ -f ~/.ws_user.conf_testbackup ] && mv -f ~/.ws_user.conf_testbackup ~/.ws_user.conf
    rm -f /tmp/ws/ws2-db/${USER}-NODURATION
}

@test "ws_allocate with duration" {
    run ws_allocate --config bats/ws.conf DURATION 7
    assert_success
    assert_output --partial "remaining time in days: 7"
    rm -f /tmp/ws/ws2-db/${USER}-DURATION
}

@test "ws_allocate with reminder, no email" {
    [ -f ~/.ws_user.conf ] && mv -f ~/.ws_user.conf ~/.ws_user.conf_testbackup
    run ws_allocate --config bats/ws.conf -r 7 REMINDER 10
    assert_output --partial "reminder email will be sent to local user account"
    assert_success
    rm -f ~/.ws_user.conf
    [ -f ~/.ws_user.conf_testbackup ] && mv -f ~/.ws_user.conf_testbackup ~/.ws_user.conf
    rm -f /tmp/ws/ws2-db/${USER}-REMINDER
}

@test "ws_allocate with reminder, invalid email" {
    run ws_allocate --config bats/ws.conf -r 1 -m a@b REMINDER
    assert_output --partial "Invalid email address"
    assert_success
    rm -f /tmp/ws/ws2-db/${USER}-REMINDER
}

@test "ws_allocate with reminder, valid email" {
    run ws_allocate --config bats/ws.conf -r 1 -m a@b.c REMINDER 10
    assert_output --partial "remaining time in days: 10"
    assert_success
    run ws_list --config bats/ws.conf -v REMINDER
    assert_output --partial "a@b.c"
    rm -f /tmp/ws/ws2-db/${USER}-REMINDER
}

@test "ws_allocate with user config for email and duration" {
    [ -f ~/.ws_user.conf ] && mv -f ~/.ws_user.conf ~/.ws_user.conf_testbackup
    echo "mail: mail@valid.domain" > ~/.ws_user.conf
    echo "duration: 14" >> ~/.ws_user.conf
    run ws_allocate --config bats/ws.conf -r 1 REMINDER
    assert_output --partial "Took email address"
    assert_output --partial "remaining time in days: 14"
    assert_success
    rm -f ~/.ws_user.conf
    [ -f ~/.ws_user.conf_testbackup ] && mv -f ~/.ws_user.conf_testbackup ~/.ws_user.conf
    rm -f /tmp/ws/ws2-db/${USER}-REMINDER
}

@test "ws_allocate with filesystem" {
    run ws_allocate --config bats/ws.conf -F ws1 WS1 10
    assert_success
    run ws_list --config bats/ws.conf -F ws1 WS1
    assert_output --partial "filesystem name      : ws1"
    rm -f /tmp/ws/ws1-db/${USER}-WS1
}

@test "ws_allocate with bad filesystem" {
    run ws_allocate --config bats/ws.conf -F wsX WS1 10
    assert_failure
    assert_output --partial "not allowed"
}

@test "ws_allocate with comment" {
    run ws_allocate --config bats/ws.conf -c "this is a comment" WS2 10
    assert_success
    run ws_list --config bats/ws.conf  WS2
    assert_output --partial "this is a comment"
    rm -f /tmp/ws/ws2-db/${USER}-WS2
}

@test "ws_allocate with group" {
    run ws_allocate --config bats/ws.conf -g -- WS2 10
    assert_success
    wsdir=$(ws_find --config bats/ws.conf WS2)
    run stat $wsdir
    assert_output --partial "drwxr-s---"
    rm -f /tmp/ws/ws2-db/${USER}-WS2
}

@test "ws_allocate with invalid group" {
    run ws_allocate --config bats/ws.conf -G INVALID_GROUP WS2 10
    assert_output --partial "invalid group specified!"
    assert_failure
}

@test "ws_allocate -x with correct group but bad workspace" {
    run ws_allocate --config bats/ws.conf -u userb -x DOES_NOT_EXIST 20
    assert_failure
    assert_output --partial "can not be extended"
}

@test "ws_allocate with -x, invalid extension, too many extensions, changing comment" {
    run ws_allocate --config bats/ws.conf -x DOES_NOT_EXIST 10
    assert_failure
    assert_output --partial "workspace does not exist, can not be extended!"

    run ws_allocate --config bats/ws.conf extensiontest 10
    assert_success
    assert_output --partial "remaining time in days: 10"

    run ws_allocate --config bats/ws.conf -x extensiontest 20
    assert_success
    assert_output --partial "extending workspace"
    assert_output --partial "remaining extensions  : 2"
    assert_output --partial "remaining time in days: 20"

    run ws_allocate --config bats/ws.conf -c "add a comment" -x extensiontest 1
    assert_success
    assert_output --partial "changed comment"
    assert_output --partial "remaining extensions  : 2"
    # FIXME: is 2 correct here??

    run ws_allocate --config bats/ws.conf -x extensiontest 5
    assert_success
    assert_output --partial "remaining extensions  : 1"

    run ws_allocate --config bats/ws.conf -x extensiontest 10
    assert_success
    assert_output --partial "remaining extensions  : 0"

    run ws_allocate --config bats/ws.conf -x extensiontest 15
    assert_failure
    assert_output --partial "no more extensions!"

    rm -f /tmp/ws/ws2-db/${USER}-extensiontest
}

@test "ws_allocate change comment (no extension)" {
    run ws_allocate --config bats/ws.conf -F ws1 TESTCOMMENT 10
    assert_success
    run ws_allocate --config bats/ws.conf -x -c "a comment" TESTCOMMENT
    assert_output --partial "changed comment"
    assert_output --partial "remaining extensions  : 3"
    run ws_list --config bats/ws.conf TESTCOMMENT
    assert_output --partial "a comment"
    ws_release --config bats/ws.conf TESTCOMMENT
}

@test "ws_allocate change mail (no extension)" {
    run ws_allocate --config bats/ws.conf -F ws1 TESTMAIL 10
    assert_success
    run ws_allocate --config bats/ws.conf -x -m "mymail@mail.com" TESTMAIL
    assert_output --partial "changed mail"
    assert_output --partial "remaining extensions  : 3"
    run ws_list --config bats/ws.conf -v TESTMAIL
    assert_output --partial "mymail"
    ws_release --config bats/ws.conf TESTMAIL
}

@test "ws_allocate -x with non-unique name" {
    ws_allocate --config bats/ws.conf -F ws1 TESTUNIQUE 10
    ws_allocate --config bats/ws.conf -F ws2 TESTUNIQUE 10
    run ws_allocate --config bats/ws.conf -x TESTUNIQUE 20
    assert_failure
    assert_output --partial "there is 2 workspaces"
    ws_release --config bats/ws.conf -F ws1 TESTUNIQUE
    ws_release --config bats/ws.conf -F ws2 TESTUNIQUE
}

@test "ws_allocate with user limit" {
    run ws_allocate --config bats/ws-with-userlimit.conf -F ws1 TESTLIMIT 1
    assert_failure
    assert_output --partial "too many workspaces"
}

@test "ws_allocate without user limit" {
    run ws_release --config bats/ws.conf -F ws1 TESTLIMIT >/dev/null 2>/dev/null
    run ws_allocate --config bats/ws.conf -F ws1 TESTLIMIT 1
    assert_success
    assert_output --partial "creating workspace"
}

@test "ws_allocate valid name with dashes" {
    run ws_allocate --config bats/ws.conf valid-name-test 5
    assert_success
    rm -f /tmp/ws/ws2-db/${USER}-valid-name-test
}

@test "ws_allocate valid name with underscores" {
    run ws_allocate --config bats/ws.conf valid_name_test 5
    assert_success
    rm -f /tmp/ws/ws2-db/${USER}-valid_name_test
}

@test "ws_allocate valid name with dots" {
    run ws_allocate --config bats/ws.conf valid.name.test 5
    assert_success
    rm -f /tmp/ws/ws2-db/${USER}-valid.name.test
}

@test "ws_allocate valid name single character" {
    run ws_allocate --config bats/ws.conf a 5
    assert_success
    rm -f /tmp/ws/ws2-db/${USER}-a
}

@test "ws_allocate valid name alphanumeric mix" {
    run ws_allocate --config bats/ws.conf Test123Mixed 5
    assert_success
    rm -f /tmp/ws/ws2-db/${USER}-Test123Mixed
}

@test "ws_allocate short form name option" {
    run ws_allocate --config bats/ws.conf -n shortname -d 5
    assert_success
    rm -f /tmp/ws/ws2-db/${USER}-shortname
}

@test "ws_allocate short form duration option" {
    run ws_allocate --config bats/ws.conf shortdur -d 7
    assert_success
    assert_output --partial "remaining time in days: 7"
    rm -f /tmp/ws/ws2-db/${USER}-shortdur
}

@test "ws_allocate short form filesystem option" {
    run ws_allocate --config bats/ws.conf -F ws1 shortfs 5
    assert_success
    rm -f /tmp/ws/ws1-db/${USER}-shortfs
}

@test "ws_allocate short form reminder option" {
    run ws_allocate --config bats/ws.conf -r 2 -m test@example.com shortrem 5
    assert_success
    rm -f /tmp/ws/ws2-db/${USER}-shortrem
}

@test "ws_allocate short form extension option" {
    ws_allocate --config bats/ws.conf shortext 5
    run ws_allocate --config bats/ws.conf -x shortext 10
    assert_success
    assert_output --partial "extending workspace"
    rm -f /tmp/ws/ws2-db/${USER}-shortext
}

@test "ws_allocate short form comment option" {
    run ws_allocate --config bats/ws.conf -c "short comment" shortcom 5
    assert_success
    rm -f /tmp/ws/ws2-db/${USER}-shortcom
}

@test "ws_allocate positional arguments" {
    run ws_allocate --config bats/ws.conf positional 14
    assert_success
    assert_output --partial "remaining time in days: 14"
    rm -f /tmp/ws/ws2-db/${USER}-positional
}

@test "ws_allocate workspace already exists" {
    ws_allocate --config bats/ws.conf EXISTS 5
    run ws_allocate --config bats/ws.conf EXISTS 10
    assert_output --partial "reusing workspace"
    assert_success
    rm -f /tmp/ws/ws2-db/${USER}-EXISTS
}

@test "ws_allocate extension with zero duration" {
    ws_allocate --config bats/ws.conf ZERODUR 10
    run ws_allocate --config bats/ws.conf -x -c "comment only" ZERODUR 0
    assert_success
    assert_output --partial "changed comment"
    rm -f /tmp/ws/ws2-db/${USER}-ZERODUR
}

@test "ws_allocate extension updates only comment" {
    ws_allocate --config bats/ws.conf ONLYCOM 10
    run ws_allocate --config bats/ws.conf -x -c "new comment" ONLYCOM
    assert_success
    assert_output --partial "changed comment"
    run ws_list --config bats/ws.conf ONLYCOM
    assert_output --partial "new comment"
    rm -f /tmp/ws/ws2-db/${USER}-ONLYCOM
}

@test "ws_allocate extension updates only mail" {
    ws_allocate --config bats/ws.conf ONLYMAIL 10
    run ws_allocate --config bats/ws.conf -x -m newemail@test.com ONLYMAIL
    assert_success
    assert_output --partial "changed mail"
    rm -f /tmp/ws/ws2-db/${USER}-ONLYMAIL
}

@test "ws_allocate extension updates comment and mail" {
    ws_allocate --config bats/ws.conf BOTH 10
    run ws_allocate --config bats/ws.conf -x -c "both" -m both@test.com BOTH
    assert_success
    assert_output --partial "changed comment"
    assert_output --partial "changed mail"
    rm -f /tmp/ws/ws2-db/${USER}-BOTH
}

@test "ws_allocate reminder warning when exceeds duration" {
    run ws_allocate --config bats/ws.conf -r 10 -m test@example.com REMWARN 5
    assert_output --partial "reminder email is sent after workspace expiry"
    assert_success
    rm -f /tmp/ws/ws2-db/${USER}-REMWARN
}

@test "ws_allocate with groupreadable creates correct permissions" {
    skip "requires specific group setup"
    ws_allocate --config bats/ws.conf -g testgroup GROUPREAD 5
    wsdir=$(ws_find --config bats/ws.conf GROUPREAD)
    run stat -c "%a" $wsdir
    # Should be 2750 or similar (readable by group)
    assert_output --regexp "27[0-5][0-9]"
    rm -f /tmp/ws/ws2-db/${USER}-GROUPREAD
}

@test "ws_allocate help message shows all options" {
    run ws_allocate --help
    assert_output --partial "duration"
    assert_output --partial "filesystem"
    assert_output --partial "reminder"
    assert_output --partial "mailaddress"
    assert_output --partial "extension"
    assert_output --partial "groupreadable"
    assert_output --partial "groupwritable"
    assert_output --partial "comment"
    assert_success
}

@test "ws_allocate config file option" {
    run ws_allocate --config bats/ws.conf conftest 5
    assert_success
    rm -f /tmp/ws/ws2-db/${USER}-conftest
}

@test "ws_allocate user config with group" {
    skip "requires user config setup"
    [ -f ~/.ws_user.conf ] && mv -f ~/.ws_user.conf ~/.ws_user.conf_testbackup
    echo "groupname: testgroup" > ~/.ws_user.conf
    run ws_allocate --config bats/ws.conf -g USERGROUP 5
    assert_output --partial "taking group"
    rm -f ~/.ws_user.conf
    [ -f ~/.ws_user.conf_testbackup ] && mv -f ~/.ws_user.conf_testbackup ~/.ws_user.conf
    rm -f /tmp/ws/ws2-db/${USER}-USERGROUP
}

@test "ws_allocate username option ignored for non-root" {
    run ws_allocate --config bats/ws.conf -u otheruser IGNOREUSR 5
    assert_output --partial "Ignoring username option"
    assert_success
    rm -f /tmp/ws/ws2-db/${USER}-IGNOREUSR
}

@test "ws_allocate creates db entry" {
    ws_allocate --config bats/ws.conf DBENTRY 5
    assert_file_exists /tmp/ws/ws2-db/${USER}-DBENTRY
    rm -f /tmp/ws/ws2-db/${USER}-DBENTRY
}

@test "ws_allocate output shows workspace path" {
    run ws_allocate --config bats/ws.conf SHOWPATH 5
    assert_output --regexp "/tmp/ws/ws2/.*SHOWPATH"
    assert_success
    rm -f /tmp/ws/ws2-db/${USER}-SHOWPATH
}

cleanup() {
    ws_release --config bats/ws.conf $ws_name
    ws_release --config bats/ws.conf TEST
}
