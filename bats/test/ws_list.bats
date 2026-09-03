setup() {
    load 'test_helper/common-setup'
    _common_setup
    ws_name="bats_workspace_test"
    export ws_name
}


@test "ws_list present" {
    which ws_list
}

# bats test_tags=broken:v1-5-0
@test "ws_list print version" {
    run ws_list --version
    assert_output --partial "ws_list"
    assert_success
}

@test "ws_list print help" {
    run ws_list --help
    assert_output --partial "Usage"
    assert_success
}

@test "ws_list shows created workspace" {
    ws_allocate --config bats/ws.conf $ws_name
    ws_list --config bats/ws.conf -s | grep $ws_name
}

@test "ws_list shows created workspace with times" {
    # create a temporary workspace for 3 days
    # prepare expected output for diff
    wsdir=$(ws_allocate --config bats/ws.conf ${ws_name}_timestamped 3)
cat <<EOF >/tmp/$$ref.txt
Id: ${ws_name}_timestamped
    workspace directory  : $wsdir
    remaining time       : 2 days, 23 hours
    creation time        : $(date +%F)
    filesystem name      : FS
    available extensions : 3
EOF
    sleep 1
    TMP=/tmp/$$tmp.txt
    # get ws_list output and parse and modify (check only day accuracy)
    ws_list --config bats/ws.conf ${ws_name}_timestamped | grep -v expiration > $TMP
    ctime=$(date +%F $(cat tmp.txt | grep "/creation time/{ print $3 }"))
    etime=$(date +%F $(cat tmp.txt | grep "/expiration date/{ print $3 }"))
    sed -i -e "s/\(.*creation time\s*:\) .*/\1 $ctime/" $TMP
    sed -i -e "s/\(.*expiration date\s*:\) .*/\1 $etime/" $TMP
    sed -i -e "s/\(.*filesystem name\s*:\) .*/\1 FS/" $TMP

    diff $TMP /tmp/$$ref.txt
    #rm tmp.txt ref.txt
#    ws_release ${ws_name}_timestamped
    rm -f /tmp/ws/ws2-db/${USER}-${ws_name}_timestamped
}

@test "ws_list sorting by name" {
    ws_allocate --config bats/ws.conf sortTestB 3
    sleep 1
    ws_allocate --config bats/ws.conf sortTestA 1
    sleep 1
    ws_allocate --config bats/ws.conf sortTestC 2

    run ws_list --config bats/ws.conf -s -N "sortTest*"
    assert_output "sortTestA
sortTestB
sortTestC"

    run ws_list --config bats/ws.conf -s -r -N "sortTest?"
    assert_output "sortTestC
sortTestB
sortTestA"

}

@test "ws_list sorting by creation" {
    #ws_allocate --config bats/ws.conf sortTestB 3
    #ws_allocate --config bats/ws.conf sortTestA 1
    #ws_allocate --config bats/ws.conf sortTestC 2

    run ws_list --config bats/ws.conf -s -C "sortTest*"
    assert_output "sortTestB
sortTestA
sortTestC"


    run ws_list --config bats/ws.conf -s -r -C "sortTest*"
    assert_output "sortTestC
sortTestA
sortTestB"
}

@test "ws_list sorting by remaining time" {
    #ws_allocate --config bats/ws.conf sortTestB 3
    #ws_allocate --config bats/ws.conf sortTestA 1
    #ws_allocate --config bats/ws.conf sortTestC 2

    run ws_list --config bats/ws.conf -s -R "sortTest[A-C]"
    assert_output "sortTestA
sortTestC
sortTestB"

    run ws_list --config bats/ws.conf -s -r -R "sortTest*"
    assert_output "sortTestB
sortTestC
sortTestA"
}

@test "ws_list -T sorting by remaining time" {
    run ws_list --config bats/ws.conf -T -R "sortTest[A-C]"
    assert_output --regexp ".*sortTestA.*
sortTestC.*
sortTestB.*$"

    run ws_list --config bats/ws.conf -T -r -R "sortTest*"
    assert_output --regexp ".*sortTestB.*
sortTestC.*
sortTestA.*$"
}

@test "ws_list -T sorting by remaining time with config directory" {
    run ws_list --config bats/ws.d -T -R "sortTest[A-C]"
    assert_output --regexp ".*sortTestA.*
sortTestC.*
sortTestB.*$"

    run ws_list --config bats/ws.d -T -r -R "sortTest*"
    assert_output --regexp ".*sortTestB.*
sortTestC.*
sortTestA.*$"
}

@test "ws_list pattern" {
    run ws_list --config bats/ws.conf -s "sort*B"
    echo "${USER}-sortTestB" | assert_output
}

@test "ws_list other fs" {
    ws_allocate --config bats/ws.conf -F ws1 WS1TEST
    run ws_list --config bats/ws.conf -s -F ws1 WS1TEST
    assert_output WS1TEST
}

@test "ws_list list fs" {
    run ws_list --config bats/ws.conf -l
    assert_output "available filesystems (sorted according to priority):
ws2
ws1"
}

@test "ws_list list fs detailed" {
    run ws_list --config bats/ws.conf -L
    assert_output --partial "ws2          32           3         7                7        true        true        true   one hell of a comment"
    assert_output --partial "ws1          31           3         7                7        true        true        true"
}

# check if sorting of config files is correct and later definition overwrites previous definition
@test "ws_list list fs detailed with config directory" {
    run ws_list --config bats/ws.d -L
    assert_output --partial "ws2          32           3         7                7        true        true        true   one hell of a comment"
    assert_output --partial "ws1          31           3         7                7        true        true        true"
    refute_output --partial "this should not"
}

@test "ws_list error handling" {
    cp /dev/null /tmp/ws/ws1-db/${USER}-WS1TEST
    run ws_list --config bats/ws.conf -F ws1
    assert_output  --partial "error"
    rm -f  /tmp/ws/ws1-db/${USER}-WS1TEST
}

@test "ws_list invalid fs" {
    run ws_list --config bats/ws.conf -F ws3
    assert_output  --partial "error"
}

@test "ws_list warn about missing adminmail in config" {
    run ws_list --config bats/bad_ws.conf
    assert_output  --partial "warning: No adminmail in config!"
    assert_success
}

@test "ws_list group workspace" {
    run ws_allocate --config bats/ws.conf -g "" -F ws1 WS1TESTGROUP
    assert_success
    run ws_list --config bats/ws.conf -g
    assert_output  --partial  "WS1TESTGROUP"
    assert_success
}

@test "ws_list short format shows only names" {
    ws_allocate --config bats/ws.conf shortformat
    run ws_list --config bats/ws.conf -s shortformat
    assert_output "shortformat"
    assert_success
    ws_release --config bats/ws.conf shortformat
}

@test "ws_list table format shows header" {
    ws_allocate --config bats/ws.conf tabletest
    run ws_list --config bats/ws.conf -T tabletest
    assert_output --partial "ID"
    assert_output --partial "PATH"
    assert_output --partial "EXPIRATION"
    assert_output --partial "REMAINING"
    assert_success
    ws_release --config bats/ws.conf tabletest
}

@test "ws_list terse table format" {
    ws_allocate --config bats/ws.conf tersefmt
    run ws_list --config bats/ws.conf -T -t tersefmt
    assert_output --partial "ID"
    assert_output --partial "PATH"
    assert_output --partial "REMAINING"
    refute_output --partial "EXPIRATION"
    assert_success
    ws_release --config bats/ws.conf tersefmt
}

@test "ws_list verbose shows reminder" {
    ws_allocate --config bats/ws.conf -r 3 -m test@example.com verbosetest 10
    run ws_list --config bats/ws.conf -v verbosetest
    assert_output --partial "reminder"
    assert_output --partial "mailaddress"
    assert_output --partial "test@example.com"
    assert_success
    ws_release --config bats/ws.conf verbosetest
}

@test "ws_list permissions shows workspace permissions" {
    ws_allocate --config bats/ws.conf permtest
    run ws_list --config bats/ws.conf -P permtest
    assert_output --partial "permissions"
    assert_output --regexp "rwx.*"
    assert_success
    ws_release --config bats/ws.conf permtest
}

@test "ws_list shows workspace comment" {
    ws_allocate --config bats/ws.conf -c "test comment" commenttest 5
    run ws_list --config bats/ws.conf commenttest
    assert_output --partial "comment"
    assert_output --partial "test comment"
    assert_success
    ws_release --config bats/ws.conf commenttest
}

@test "ws_list shows creation time" {
    ws_allocate --config bats/ws.conf createtime
    run ws_list --config bats/ws.conf createtime
    assert_output --partial "creation time"
    assert_success
    ws_release --config bats/ws.conf createtime
}

@test "ws_list shows expiration time" {
    ws_allocate --config bats/ws.conf expiretime
    run ws_list --config bats/ws.conf expiretime
    assert_output --partial "expiration time"
    assert_success
    ws_release --config bats/ws.conf expiretime
}

@test "ws_list shows filesystem name" {
    ws_allocate --config bats/ws.conf fsname
    run ws_list --config bats/ws.conf fsname
    assert_output --partial "filesystem name"
    assert_success
    ws_release --config bats/ws.conf fsname
}

@test "ws_list shows available extensions" {
    ws_allocate --config bats/ws.conf exttest
    run ws_list --config bats/ws.conf exttest
    assert_output --partial "available extensions"
    assert_success
    ws_release --config bats/ws.conf exttest
}

@test "ws_list shows remaining time in days and hours" {
    ws_allocate --config bats/ws.conf remaintest 5
    run ws_list --config bats/ws.conf remaintest
    assert_output --partial "remaining time"
    assert_output --partial "days"
    assert_output --partial "hours"
    assert_success
    ws_release --config bats/ws.conf remaintest
}

@test "ws_list positional pattern argument" {
    ws_allocate --config bats/ws.conf pospattern
    run ws_list --config bats/ws.conf -s pospattern
    assert_output "pospattern"
    assert_success
    ws_release --config bats/ws.conf pospattern
}

@test "ws_list pattern with wildcards" {
    ws_allocate --config bats/ws.conf wildcard1
    ws_allocate --config bats/ws.conf wildcard2
    run ws_list --config bats/ws.conf -s "wildcard*"
    assert_output --partial "wildcard1"
    assert_output --partial "wildcard2"
    assert_success
    ws_release --config bats/ws.conf wildcard1
    ws_release --config bats/ws.conf wildcard2
}

@test "ws_list pattern with single char wildcard" {
    ws_allocate --config bats/ws.conf single1
    ws_allocate --config bats/ws.conf single2
    run ws_list --config bats/ws.conf -s "single?"
    assert_output --partial "single1"
    assert_output --partial "single2"
    assert_success
    ws_release --config bats/ws.conf single1
    ws_release --config bats/ws.conf single2
}

@test "ws_list no matching workspaces" {
    run ws_list --config bats/ws.conf -s "nomatch$$"
    # Should return no output or empty
    refute_output --partial "nomatch"
    assert_success
}

@test "ws_list multiple workspaces" {
    ws_allocate --config bats/ws.conf multi1
    ws_allocate --config bats/ws.conf multi2
    ws_allocate --config bats/ws.conf multi3
    run ws_list --config bats/ws.conf -s "multi*"
    assert_output --partial "multi1"
    assert_output --partial "multi2"
    assert_output --partial "multi3"
    assert_success
    ws_release --config bats/ws.conf multi1
    ws_release --config bats/ws.conf multi2
    ws_release --config bats/ws.conf multi3
}

@test "ws_list default pattern shows all" {
    ws_allocate --config bats/ws.conf defaultpat
    run ws_list --config bats/ws.conf -s
    assert_output --partial "defaultpat"
    assert_success
    ws_release --config bats/ws.conf defaultpat
}

@test "ws_list filesystem details with verbose" {
    run ws_list --config bats/ws.conf -L -v
    assert_output --partial "Explanation:"
    assert_output --partial "maxduration:"
    assert_output --partial "extensions:"
    assert_output --partial "keeptime:"
    assert_success
}

@test "ws_list short form options" {
    ws_allocate --config bats/ws.conf shortopt
    run ws_list --config bats/ws.conf -s shortopt
    assert_output "shortopt"
    assert_success
    ws_release --config bats/ws.conf shortopt
}

@test "ws_list mean name" {
    ws_allocate --config bats/ws.conf mean-name
    run ws_list --config bats/ws.conf -s mean-name
    assert_output "mean-name"
    assert_success
    ws_release --config bats/ws.conf "mean-name"
}

@test "ws_list table format with NO_COLOR" {
    ws_allocate --config bats/ws.conf colortest
    run env NO_COLOR=1 ws_list --config bats/ws.conf -T colortest
    # Should work without errors
    assert_success
    ws_release --config bats/ws.conf colortest
}

@test "ws_list config option works" {
    ws_allocate --config bats/ws.conf confopt
    run ws_list --config bats/ws.conf -s confopt
    assert_output "confopt"
    assert_success
    ws_release --config bats/ws.conf confopt
}

@test "ws_list help shows all options" {
    run ws_list --help
    assert_output --partial "filesystem"
    assert_output --partial "group"
    assert_output --partial "short"
    assert_output --partial "sort"
    assert_output --partial "pattern"
    assert_output --partial "verbose"
    assert_output --partial "permissions"
    assert_success
}

@test "ws_list traditional format multiple lines" {
    ws_allocate --config bats/ws.conf tradformat
    run ws_list --config bats/ws.conf tradformat
    assert_output --partial "Id:"
    assert_output --partial "workspace directory"
    assert_output --partial "remaining time"
    assert_output --partial "creation time"
    assert_output --partial "expiration time"
    assert_output --partial "filesystem name"
    assert_output --partial "available extensions"
    assert_success
    ws_release --config bats/ws.conf tradformat
}

@test "ws_list terse format omits some fields" {
    ws_allocate --config bats/ws.conf -c "comment" tersefmt2
    run ws_list --config bats/ws.conf -t tersefmt2
    assert_output --partial "workspace directory"
    assert_output --partial "remaining time"
    refute_output --partial "comment"
    refute_output --partial "creation time"
    refute_output --partial "expiration time"
    assert_success
    ws_release --config bats/ws.conf tersefmt2
}

@test "ws_list list filesystems shows priority order" {
    run ws_list --config bats/ws.conf -l
    assert_output --partial "sorted according to priority"
    assert_success
}

@test "ws_list filesystem details shows allocatable flag" {
    run ws_list --config bats/ws.conf -L
    assert_output --partial "allocatable"
    assert_success
}

@test "ws_list filesystem details shows extendable flag" {
    run ws_list --config bats/ws.conf -L
    assert_output --partial "extendable"
    assert_success
}

@test "ws_list filesystem details shows restorable flag" {
    run ws_list --config bats/ws.conf -L
    assert_output --partial "restorable"
    assert_success
}

@test "ws_list workspace on specific filesystem" {
    ws_allocate --config bats/ws.conf -F ws1 specfs
    run ws_list --config bats/ws.conf -F ws1 -s specfs
    assert_output "specfs"
    assert_success
    ws_release --config bats/ws.conf -F ws1 specfs
}

@test "ws_list different workspaces different filesystems" {
    ws_allocate --config bats/ws.conf -F ws1 difffs1
    ws_allocate --config bats/ws.conf -F ws2 difffs2

    run ws_list --config bats/ws.conf -F ws1 -s difffs1
    assert_output "difffs1"

    run ws_list --config bats/ws.conf -F ws2 -s difffs2
    assert_output "difffs2"

    assert_success
    ws_release --config bats/ws.conf -F ws1 difffs1
    ws_release --config bats/ws.conf -F ws2 difffs2
}

@test "ws_list shows workspace path" {
    ws_allocate --config bats/ws.conf pathtest
    run ws_list --config bats/ws.conf pathtest
    assert_output --regexp "/tmp/ws/ws[12]/.*pathtest"
    assert_success
    ws_release --config bats/ws.conf pathtest
}

cleanup() {
    ws_release --config bats/ws.conf $ws_name
    assert_failure
}
