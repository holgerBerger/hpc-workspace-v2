setup() {
    load 'test_helper/common-setup'
    _common_setup
}

# create a DB entry in the recovery area of ws2 and its workspace directory,
# the comment of the DB entry and a marker file in the directory hold the original timestamp,
# to verify that DB entry and directory still belong together after renames
# usage: _mk_deleted NAME TS EXPIRATION [EXPIRED]   (without EXPIRED: v1 style entry)
_mk_deleted() {
    local name="$1" ts="$2" expiration="$3" expired="$4"
    mkdir -p /tmp/ws/ws2/1/.removed
    {
        echo "workspace: /tmp/ws/ws2/1/${USER}-${name}"
        echo "creation: $((ts - 200000))"
        echo "expiration: ${expiration}"
        if [ -n "$expired" ]; then
            echo "expired: ${expired}"
        fi
        echo "extensions: 0"
        echo "reminder: 0"
        echo 'mailaddress: ""'
        echo "comment: \"${ts}\""
    } > "/tmp/ws/ws2-db/.removed/${USER}-${name}-${ts}"
    mkdir -p "/tmp/ws/ws2/1/.removed/${USER}-${name}-${ts}"
    echo "${ts}" > "/tmp/ws/ws2/1/.removed/${USER}-${name}-${ts}/marker"
}

# remove all DB entries and workspace directories of NAME from the recovery area of ws2
_rm_deleted() {
    rm -rf "/tmp/ws/ws2-db/.removed/${USER}-$1-"* "/tmp/ws/ws2/1/.removed/${USER}-$1-"*
}

# check that DB entry and workspace directory NAME-TS exist and belong together
_check_pair() {
    local name="$1" ts="$2"
    local entry="/tmp/ws/ws2-db/.removed/${USER}-${name}-${ts}"
    local wsdir="/tmp/ws/ws2/1/.removed/${USER}-${name}-${ts}"
    [ -f "$entry" ]
    [ -f "$wsdir/marker" ]
    local orig
    orig=$(cat "$wsdir/marker")
    grep -E "^comment: [\"']?${orig}[\"']?$" "$entry"
}

@test "ws_editdb present" {
    which ws_editdb
}

@test "ws_editdb print version" {
    run ws_editdb --version
    assert_output --partial "ws_editdb"
    assert_success
}

@test "ws_editdb print help" {
    run ws_editdb --help
    assert_output --partial "Usage"
    assert_success
}

@test "ws_editdb dryrun" {
    ws_allocate --config bats/ws.conf EDITTEST 1
    run ws_editdb --config bats/ws.conf --add-time 1 EDITTEST
    assert_output --partial "change expiration"
    assert_success
}

@test "ws_editdb not kidding" {
    run ws_editdb --config bats/ws.conf --not-kidding --add-time 1 EDITTEST
    assert_output --partial "change expiration"
    assert_success
    run ws_list --config bats/ws.conf  EDITTEST
    assert_output --regexp "(2 days)|1 days, 23"
    assert_success
}

@test "ws_editdb ensure-until" {
    run ws_editdb --config bats/ws.conf --not-kidding --ensure-until 2050-12-31 EDITTEST
    assert_output --partial "change expiration"
    assert_success
    run ws_list --config bats/ws.conf EDITTEST
    assert_output --partial "expiration time      : Sat Dec 31 23:59:59 2050"
    assert_success
}

@test "ws_editdb expire-by" {
    run ws_editdb --config bats/ws.conf --not-kidding --expire-by 2049-12-31 EDITTEST
    assert_output --partial "change expiration"
    assert_success
    run ws_list --config bats/ws.conf EDITTEST
    assert_output --partial "expiration time      : Fri Dec 31 00:00:00 2049"
    assert_success
    ws_release --config bats/ws.conf EDITTEST
}

# Test --add-time-expired functionality
@test "ws_editdb add-time-expired dryrun" {
    ws_allocate --config bats/ws.conf EXPIREDTEST 1
    run ws_editdb --config bats/ws.conf --add-time-expired 5 EXPIREDTEST
    assert_output --partial "change expired"
    assert_success
}

@test "ws_editdb add-time-expired not kidding" {
    run ws_editdb --config bats/ws.conf --not-kidding --add-time-expired 10 EXPIREDTEST
    assert_output --partial "change expired"
    assert_success
    ws_release --config bats/ws.conf EXPIREDTEST
}

# Test pattern matching
@test "ws_editdb pattern matching with wildcard" {
    ws_allocate --config bats/ws.conf PATTERN_TEST1 1
    ws_allocate --config bats/ws.conf PATTERN_TEST2 1
    ws_allocate --config bats/ws.conf OTHER_NAME 1
    run ws_editdb --config bats/ws.conf --add-time 1 "PATTERN_*"
    assert_output --partial "PATTERN_TEST1"
    assert_output --partial "PATTERN_TEST2"
    refute_output --partial "OTHER_NAME"
    assert_success
    ws_release --config bats/ws.conf PATTERN_TEST1
    ws_release --config bats/ws.conf PATTERN_TEST2
    ws_release --config bats/ws.conf OTHER_NAME
}

@test "ws_editdb pattern matching exact name" {
    ws_allocate --config bats/ws.conf EXACT_MATCH 1
    ws_allocate --config bats/ws.conf EXACT_MATCH_OTHER 1
    run ws_editdb --config bats/ws.conf --add-time 1 "EXACT_MATCH"
    assert_output --partial "EXACT_MATCH"
    refute_output --partial "EXACT_MATCH_OTHER"
    assert_success
    ws_release --config bats/ws.conf EXACT_MATCH
    ws_release --config bats/ws.conf EXACT_MATCH_OTHER
}

# Test conflicting options
@test "ws_editdb multiple adds" {
    ws_allocate --config bats/ws.conf CONFLICT_TEST 1
    run ws_editdb --config bats/ws.conf --not-kidding --expire-by 2049-12-31 --add-time 1 CONFLICT_TEST
    assert_output --partial "Only one"
    assert_failure
    ws_release --config bats/ws.conf CONFLICT_TEST
}

@test "ws_editdb add-time and add-time-expired conflict" {
    ws_allocate --config bats/ws.conf CONFLICT_TEST2 1
    run ws_editdb --config bats/ws.conf --add-time 1 --add-time-expired 1 CONFLICT_TEST2
    assert_output --partial "Only one"
    assert_failure
    ws_release --config bats/ws.conf CONFLICT_TEST2
}

@test "ws_editdb ensure-until and expire-by conflict" {
    ws_allocate --config bats/ws.conf CONFLICT_TEST3 1
    run ws_editdb --config bats/ws.conf --ensure-until 2050-12-31 --expire-by 2049-12-31 CONFLICT_TEST3
    assert_output --partial "Only one"
    assert_failure
    ws_release --config bats/ws.conf CONFLICT_TEST3
}

@test "ws_editdb add-time and ensure-until conflict" {
    ws_allocate --config bats/ws.conf CONFLICT_TEST4 1
    run ws_editdb --config bats/ws.conf --add-time 1 --ensure-until 2050-12-31 CONFLICT_TEST4
    assert_output --partial "Only one"
    assert_failure
    ws_release --config bats/ws.conf CONFLICT_TEST4
}

# Test dry-run vs not-kidding conflict
@test "ws_editdb dry-run and not-kidding conflict" {
    ws_allocate --config bats/ws.conf DRYRUN_TEST 1
    run ws_editdb --config bats/ws.conf --dry-run --not-kidding --add-time 1 DRYRUN_TEST
    assert_output --partial "either"
    assert_success
    ws_release --config bats/ws.conf DRYRUN_TEST
}

# Test date parsing
@test "ws_editdb invalid date format" {
    ws_allocate --config bats/ws.conf DATETEST 1
    run ws_editdb --config bats/ws.conf --expire-by "invalid-date" DATETEST
    assert_output --partial "parsing failed"
    assert_failure
    ws_release --config bats/ws.conf DATETEST
}

@test "ws_editdb valid date formats" {
    ws_allocate --config bats/ws.conf DATETEST2 1
    run ws_editdb --config bats/ws.conf --ensure-until 2048-01-15 DATETEST2
    assert_output --partial "change expiration"
    assert_success
    ws_release --config bats/ws.conf DATETEST2
}

# Test ensure-until behavior (only extends, doesn't shrink)
@test "ws_editdb ensure-until does not shrink" {
    ws_allocate --config bats/ws.conf ENSURE_TEST 1
    # First extend it far into the future
    ws_editdb --config bats/ws.conf --not-kidding --ensure-until 2060-12-31 ENSURE_TEST
    # Try to "ensure" earlier date - should not change anything
    run ws_editdb --config bats/ws.conf --not-kidding --ensure-until 2040-12-31 ENSURE_TEST
    refute_output --partial "change expiration"
    assert_success
    ws_release --config bats/ws.conf ENSURE_TEST
}

# Test expire-by behavior (only shrinks, doesn't extend)
@test "ws_editdb expire-by does not extend" {
    ws_allocate --config bats/ws.conf EXPIRE_TEST 1
    # Try to expire by far future date - should not change anything since workspace expires sooner
    run ws_editdb --config bats/ws.conf --not-kidding --expire-by 2060-12-31 EXPIRE_TEST
    refute_output --partial "change expiration"
    assert_success
    ws_release --config bats/ws.conf EXPIRE_TEST
}

# Test negative add-time
@test "ws_editdb negative add-time" {
    ws_allocate --config bats/ws.conf NEGATIVE_TIME 1
    run ws_editdb --config bats/ws.conf --not-kidding --add-time -1 NEGATIVE_TIME
    assert_output --partial "change expiration"
    assert_success
    ws_release --config bats/ws.conf NEGATIVE_TIME
}

# Test with no pattern (should match all)
@test "ws_editdb no pattern matches all" {
    ws_allocate --config bats/ws.conf NOPATTERN1 1
    ws_allocate --config bats/ws.conf NOPATTERN2 1
    run ws_editdb --config bats/ws.conf --add-time 1
    assert_output --partial "NOPATTERN1"
    assert_output --partial "NOPATTERN2"
    assert_success
    ws_release --config bats/ws.conf NOPATTERN1
    ws_release --config bats/ws.conf NOPATTERN2
}

# Test output format includes workspace path
@test "ws_editdb output shows workspace path" {
    ws_allocate --config bats/ws.conf PATHTEST 1
    run ws_editdb --config bats/ws.conf --add-time 1 PATHTEST
    assert_output --regexp "Id:.*\\(.*PATHTEST.*\\)"
    assert_success
    ws_release --config bats/ws.conf PATHTEST
}

# Test dry-run is default
@test "ws_editdb dry-run is default" {
    ws_allocate --config bats/ws.conf DRYDEFAULT 1
    run ws_editdb --config bats/ws.conf --add-time 1 DRYDEFAULT
    assert_output --partial "Actions that would be performed"
    assert_success
    # Verify nothing actually changed
    run ws_list --config bats/ws.conf DRYDEFAULT
    assert_output --regexp "(1 day)|(0 days, 23 hours)"
    assert_success
    ws_release --config bats/ws.conf DRYDEFAULT
}

# Test filesystem option
@test "ws_editdb with filesystem option" {
    ws_allocate --config bats/ws.conf FSTEST 1
    run ws_editdb --config bats/ws.conf --filesystem lustre01 --add-time 1 FSTEST
    assert_success
    ws_release --config bats/ws.conf FSTEST
}

# Test zero add-time (should do nothing)
@test "ws_editdb zero add-time" {
    ws_allocate --config bats/ws.conf ZEROTEST 1
    run ws_editdb --config bats/ws.conf --add-time 0 ZEROTEST
    refute_output --partial "change expiration"
    assert_success
    ws_release --config bats/ws.conf ZEROTEST
}

# Test large add-time value
@test "ws_editdb large add-time" {
    ws_allocate --config bats/ws.conf LARGETEST 1
    run ws_editdb --config bats/ws.conf --not-kidding --add-time 365 LARGETEST
    assert_output --partial "change expiration"
    assert_success
    ws_release --config bats/ws.conf LARGETEST
}

# Test that non-existent workspace pattern returns gracefully
@test "ws_editdb non-existent workspace" {
    run ws_editdb --config bats/ws.conf --add-time 1 "DOESNOTEXIST12345"
    refute_output --partial "change expiration"
    assert_success
}

# Test multiple workspaces modification
@test "ws_editdb modify multiple workspaces" {
    ws_allocate --config bats/ws.conf MULTI1 1
    ws_allocate --config bats/ws.conf MULTI2 1
    ws_allocate --config bats/ws.conf MULTI3 1
    run ws_editdb --config bats/ws.conf --not-kidding --add-time 2 "MULTI*"
    assert_output --partial "MULTI1"
    assert_output --partial "MULTI2"
    assert_output --partial "MULTI3"
    assert_success
    # Verify all were modified
    run ws_list --config bats/ws.conf MULTI1
    assert_output --regexp "(3 days)|2 days, 23"
    assert_success
    ws_release --config bats/ws.conf MULTI1
    ws_release --config bats/ws.conf MULTI2
    ws_release --config bats/ws.conf MULTI3
}

# Test with expired workspaces flag
@test "ws_editdb with expired flag" {
    ws_allocate --config bats/ws.conf EXPFLAG 1
    run ws_editdb --config bats/ws.conf --expired --add-time 1 EXPFLAG
    assert_success
    ws_release --config bats/ws.conf EXPFLAG
}

# Test ensure-until with past date
@test "ws_editdb ensure-until with past date" {
    ws_allocate --config bats/ws.conf PASTDATE 1
    run ws_editdb --config bats/ws.conf --not-kidding --ensure-until 2020-01-01 PASTDATE
    refute_output --partial "change expiration"
    assert_success
    ws_release --config bats/ws.conf PASTDATE
}

# Test expire-by with past date
@test "ws_editdb expire-by with past date" {
    ws_allocate --config bats/ws.conf PASTEXPIRE 1
    run ws_editdb --config bats/ws.conf --not-kidding --expire-by 2020-01-01 PASTEXPIRE
    assert_output --partial "change expiration"
    assert_success
    ws_release --config bats/ws.conf PASTEXPIRE
}

# Test renaming of deleted entries when --add-time-expired changes the expired time
@test "ws_editdb add-time-expired shows rename in dryrun" {
    ts=$(date +%s)
    new_ts=$((ts + 5 * 86400))
    entry="/tmp/ws/ws2-db/.removed/${USER}-MANUALDRY-${ts}"
    wsdir="/tmp/ws/ws2/1/.removed/${USER}-MANUALDRY-${ts}"
    mkdir -p /tmp/ws/ws2/1/.removed
    cat > "$entry" <<EOF
workspace: /tmp/ws/ws2/1/${USER}-MANUALDRY
creation: $((ts - 200000))
expiration: $((ts - 86400))
expired: ${ts}
extensions: 0
reminder: 0
mailaddress: ""
comment: ""
EOF
    mkdir -p "$wsdir"

    run ws_editdb --config bats/ws.conf -e --add-time-expired 5 "MANUALDRY*"
    assert_output --partial "rename db entry"
    assert_output --partial "${USER}-MANUALDRY-${new_ts}"
    assert_output --partial "move workspace"
    assert_success
    # dry-run must not change anything
    [ -e "$entry" ]
    [ -d "$wsdir" ]
    rm -rf "$entry" "$wsdir"
}

@test "ws_editdb add-time-expired renames deleted entry and workspace" {
    ts=$(date +%s)
    new_ts=$((ts + 5 * 86400))
    entry="/tmp/ws/ws2-db/.removed/${USER}-MANUALDEL-${ts}"
    wsdir="/tmp/ws/ws2/1/.removed/${USER}-MANUALDEL-${ts}"
    mkdir -p /tmp/ws/ws2/1/.removed
    cat > "$entry" <<EOF
workspace: /tmp/ws/ws2/1/${USER}-MANUALDEL
creation: $((ts - 200000))
expiration: $((ts - 86400))
expired: ${ts}
extensions: 0
reminder: 0
mailaddress: ""
comment: ""
EOF
    mkdir -p "$wsdir"

    run ws_editdb --config bats/ws.conf -e --not-kidding --add-time-expired 5 "MANUALDEL*"
    assert_output --partial "rename db entry"
    assert_success
    [ ! -e "$entry" ]
    [ ! -d "$wsdir" ]
    [ -e "/tmp/ws/ws2-db/.removed/${USER}-MANUALDEL-${new_ts}" ]
    [ -d "/tmp/ws/ws2/1/.removed/${USER}-MANUALDEL-${new_ts}" ]
    # content holds the new expired time
    run cat "/tmp/ws/ws2-db/.removed/${USER}-MANUALDEL-${new_ts}"
    assert_output --partial "expired: ${new_ts}"
    rm -rf "/tmp/ws/ws2-db/.removed/${USER}-MANUALDEL-${new_ts}" "/tmp/ws/ws2/1/.removed/${USER}-MANUALDEL-${new_ts}"
}

# released workspaces carry the release time in their name, they must not be renamed
@test "ws_editdb does not rename released workspaces" {
    ts=$(date +%s)
    entry="/tmp/ws/ws2-db/.removed/${USER}-RELNOREN-${ts}"
    wsdir="/tmp/ws/ws2/1/.removed/${USER}-RELNOREN-${ts}"
    mkdir -p /tmp/ws/ws2/1/.removed
    cat > "$entry" <<EOF
workspace: /tmp/ws/ws2/1/${USER}-RELNOREN
creation: $((ts - 200000))
expiration: $((ts - 100000))
released: ${ts}
extensions: 0
reminder: 0
mailaddress: ""
comment: ""
EOF
    mkdir -p "$wsdir"

    run ws_editdb --config bats/ws.conf -e --not-kidding --add-time-expired 5 "RELNOREN*"
    assert_output --partial "NOT renamed"
    assert_success
    [ -e "$entry" ]
    [ -d "$wsdir" ]
    rm -rf "$entry" "$wsdir"
}

# without an actual modification nothing must be renamed, especially not v1 entries (no expired
# field, expiration earlier than name timestamp) and entries where expired and name timestamp
# differ slightly (as ws_expirer takes the time twice)
@test "ws_editdb -e without modification does not rename" {
    ts=$(date +%s)
    _rm_deleted RENNOOP1
    _rm_deleted RENNOOP2
    _mk_deleted RENNOOP1 "$ts" "$((ts - 3 * 86400))"
    _mk_deleted RENNOOP2 "$ts" "$((ts - 86400))" "$((ts + 1))"

    run ws_editdb --config bats/ws.conf -e --not-kidding "RENNOOP*"
    refute_output --partial "rename db entry"
    assert_success

    # expire-by in the far future changes nothing either
    run ws_editdb --config bats/ws.conf -e --not-kidding --expire-by 2060-12-31 "RENNOOP*"
    refute_output --partial "rename db entry"
    assert_success

    _check_pair RENNOOP1 "$ts"
    _check_pair RENNOOP2 "$ts"
    _rm_deleted RENNOOP1
    _rm_deleted RENNOOP2
}

# v1 entry: extending expiration, but not beyond the name timestamp, does not move the boundary
@test "ws_editdb -e add-time on v1 entry within boundary does not rename" {
    ts=$(date +%s)
    _rm_deleted RENV1ADD
    _mk_deleted RENV1ADD "$ts" "$((ts - 3 * 86400))"

    run ws_editdb --config bats/ws.conf -e --not-kidding --add-time 1 "RENV1ADD*"
    assert_output --partial "change expiration"
    assert_output --partial "note: retention unchanged, still determined by time encoded in name"
    refute_output --partial "rename db entry"
    assert_success

    _check_pair RENV1ADD "$ts"
    run cat "/tmp/ws/ws2-db/.removed/${USER}-RENV1ADD-${ts}"
    assert_output --partial "expiration: $((ts - 2 * 86400))"
    _rm_deleted RENV1ADD
}

# v1 entry has no expired field, add-time-expired has to start from the name timestamp, not from 0
@test "ws_editdb -e add-time-expired on v1 entry starts from name timestamp" {
    ts=$(date +%s)
    new_ts=$((ts + 5 * 86400))
    _rm_deleted RENV1EXP
    _mk_deleted RENV1EXP "$ts" "$((ts - 3 * 86400))"

    run ws_editdb --config bats/ws.conf -e --not-kidding --add-time-expired 5 "RENV1EXP*"
    assert_output --partial "change expired"
    assert_output --partial "(${new_ts})"
    assert_output --partial "restorable until:"
    refute_output --partial "retention unchanged"
    assert_output --partial "rename db entry: ${USER}-RENV1EXP-${ts} -> ${USER}-RENV1EXP-${new_ts}"
    assert_success

    [ ! -e "/tmp/ws/ws2-db/.removed/${USER}-RENV1EXP-${ts}" ]
    [ ! -e "/tmp/ws/ws2/1/.removed/${USER}-RENV1EXP-${ts}" ]
    _check_pair RENV1EXP "$new_ts"
    run cat "/tmp/ws/ws2-db/.removed/${USER}-RENV1EXP-${new_ts}"
    assert_output --partial "expired: ${new_ts}"
    _rm_deleted RENV1EXP
}

# ensure-until gives several entries of the same workspace name the same boundary,
# names have to be made unique without overwriting anything, also in dry-run output
@test "ws_editdb -e ensure-until resolves name collisions" {
    ts=$(date +%s)
    d=$(date -d "2050-12-31 23:59:59" +%s)
    _rm_deleted RENCOL
    _mk_deleted RENCOL "$((ts - 30))" "$((ts - 86400))" "$((ts - 30))"
    _mk_deleted RENCOL "$((ts - 20))" "$((ts - 86400))" "$((ts - 20))"
    _mk_deleted RENCOL "$((ts - 10))" "$((ts - 86400))" "$((ts - 10))"

    run ws_editdb --config bats/ws.conf -e --ensure-until 2050-12-31 "RENCOL*"
    assert_output --partial "-> ${USER}-RENCOL-${d}"
    assert_output --partial "-> ${USER}-RENCOL-$((d + 1))"
    assert_output --partial "-> ${USER}-RENCOL-$((d + 2))"
    assert_success
    # dry-run changed nothing
    _check_pair RENCOL "$((ts - 30))"
    _check_pair RENCOL "$((ts - 20))"
    _check_pair RENCOL "$((ts - 10))"

    run ws_editdb --config bats/ws.conf -e --not-kidding --ensure-until 2050-12-31 "RENCOL*"
    refute_output --partial "error"
    assert_success
    _check_pair RENCOL "$d"
    _check_pair RENCOL "$((d + 1))"
    _check_pair RENCOL "$((d + 2))"
    [ ! -e "/tmp/ws/ws2-db/.removed/${USER}-RENCOL-$((ts - 30))" ]
    [ ! -e "/tmp/ws/ws2-db/.removed/${USER}-RENCOL-$((ts - 20))" ]
    [ ! -e "/tmp/ws/ws2-db/.removed/${USER}-RENCOL-$((ts - 10))" ]
    [ ! -e "/tmp/ws/ws2/1/.removed/${USER}-RENCOL-$((ts - 30))" ]
    [ ! -e "/tmp/ws/ws2/1/.removed/${USER}-RENCOL-$((ts - 20))" ]
    [ ! -e "/tmp/ws/ws2/1/.removed/${USER}-RENCOL-$((ts - 10))" ]

    # running again is a no-op
    run ws_editdb --config bats/ws.conf -e --not-kidding --ensure-until 2050-12-31 "RENCOL*"
    refute_output --partial "rename db entry"
    assert_success
    _rm_deleted RENCOL
}

# lowering expired below expiration does not change deletion time, the admin gets told so
@test "ws_editdb -e reports unchanged retention" {
    ts=$(date +%s)
    _rm_deleted RENNOTE
    # expiration later than expired, e.g. after an earlier ensure-until
    _mk_deleted RENNOTE "$ts" "$ts" "$((ts - 86400))"

    run ws_editdb --config bats/ws.conf -e --not-kidding --add-time-expired -10 "RENNOTE*"
    assert_output --partial "change expired"
    assert_output --partial "note: retention unchanged, still determined by expiration"
    refute_output --partial "rename db entry"
    assert_success
    _check_pair RENNOTE "$ts"
    _rm_deleted RENNOTE
}

# a workspace directory without DB entry blocks the target name, it must not be overwritten
@test "ws_editdb -e does not overwrite existing workspace directory" {
    ts=$(date +%s)
    new_ts=$((ts + 5 * 86400))
    _rm_deleted RENBLK
    _mk_deleted RENBLK "$ts" "$((ts - 86400))" "$ts"
    mkdir -p "/tmp/ws/ws2/1/.removed/${USER}-RENBLK-${new_ts}"
    echo blocker > "/tmp/ws/ws2/1/.removed/${USER}-RENBLK-${new_ts}/blocker"

    run ws_editdb --config bats/ws.conf -e --not-kidding --add-time-expired 5 "RENBLK*"
    assert_output --partial "-> ${USER}-RENBLK-$((new_ts + 1))"
    assert_success

    _check_pair RENBLK "$((new_ts + 1))"
    [ ! -e "/tmp/ws/ws2-db/.removed/${USER}-RENBLK-${new_ts}" ]
    run cat "/tmp/ws/ws2/1/.removed/${USER}-RENBLK-${new_ts}/blocker"
    assert_output "blocker"
    _rm_deleted RENBLK
}
