# bats file_tags=completion
#
# Tests for the bash completion in tools/bash-completion (completions/ and helpers/).
#
# The completion functions are called directly with a prepared COMP_WORDS/COMP_CWORD.
# The ws_* tools they call are replaced by stubs returning fixed data, so these tests
# need neither a build nor a workspace configuration.

setup() {
    bats_load_library 'bats-support'
    bats_load_library 'bats-assert'

    PROJECT_ROOT="$( cd "$( dirname "$BATS_TEST_FILENAME" )/../.." >/dev/null 2>&1 && pwd )"
    COMPLETION_DIR="$PROJECT_ROOT/tools/bash-completion"
    COMPLETION_TOOLS=(ws_allocate ws_find ws_list ws_extend ws_release ws_restore ws_share)

    STUB_DIR="$BATS_TEST_TMPDIR/bin"
    STUB_LOG="$BATS_TEST_TMPDIR/calls"
    export STUB_LOG
    _create_stubs
    PATH="$STUB_DIR:$PATH"

    # predefine the variables set by complete_words, so they do not count as leaked globals
    COMP_WORDS=() COMP_CWORD=0 COMP_LINE="" COMP_POINT=0 COMPREPLY=()
}

# load_completion CMD - source the completion file of CMD, as the bash-completion loader does
load_completion() {
    source "$COMPLETION_DIR/completions/$1.bash"
}

# Stub data:
#   filesystems: ws1, ws2
#   workspaces:  ws1: alpha beta, ws2: gamma
#   restorable:  user-alpha-1700000000 (ws1), user-delta-1700000001 (ws2)
#   shares:      alpha is shared with user alice and group staff
# Setting STUB_FAIL=1 makes all stubs print an error to stderr and fail.
_create_stubs() {
    mkdir -p "$STUB_DIR"

    cat > "$STUB_DIR/ws_list" <<'EOF'
#!/bin/bash
echo "ws_list $*" >> "$STUB_LOG"
[[ -n $STUB_FAIL ]] && { echo "error: stub failure" >&2; exit 1; }
fs=""
while (( $# )); do
    case $1 in
        -l) printf 'available filesystems (sorted according to priority):\nws1\nws2\n'; exit 0 ;;
        -F|--filesystem) fs=$2; shift ;;
    esac
    shift
done
case $fs in
    "")  printf 'alpha\nbeta\ngamma\n' ;;
    ws1) printf 'alpha\nbeta\n' ;;
    ws2) printf 'gamma\n' ;;
esac
EOF

    cat > "$STUB_DIR/ws_restore" <<'EOF'
#!/bin/bash
echo "ws_restore $*" >> "$STUB_LOG"
[[ -n $STUB_FAIL ]] && { echo "error: stub failure" >&2; exit 1; }
case " $* " in *" -F ws1 "*) echo user-alpha-1700000000 ;; *" -F ws2 "*) echo user-delta-1700000001 ;; *) printf 'user-alpha-1700000000\nuser-delta-1700000001\n' ;; esac
EOF

    # output format of 'ws_share list', see bin/ws_share
    cat > "$STUB_DIR/ws_share" <<'EOF'
#!/bin/bash
echo "ws_share $*" >> "$STUB_LOG"
[[ -n $STUB_FAIL ]] && { echo "error: stub failure" >&2; exit 1; }
printf '%-7s %-20s %s\n' user alice read group staff readwrite
EOF

    chmod +x "$STUB_DIR"/*
}

# complete_words CMD WORD... CUR
# Simulates pressing TAB with the cursor at the end of the last word (use "" for an empty word).
# The completion file of CMD is loaded on first use, the registered function is looked up with 'complete -p'.
complete_words() {
    complete -p "$1" &>/dev/null || load_completion "$1"
    COMP_WORDS=("$@")
    COMP_CWORD=$(( ${#COMP_WORDS[@]} - 1 ))
    COMP_LINE="${COMP_WORDS[*]}"
    COMP_POINT=${#COMP_LINE}
    COMPREPLY=()
    local func
    func=$(complete -p "$1" | sed -E 's/.* -F ([^ ]+) .*/\1/')
    "$func" "$1" "${COMP_WORDS[COMP_CWORD]}" "${COMP_WORDS[COMP_CWORD - 1]}" || true
}

# assert_completions CANDIDATE... - COMPREPLY contains exactly these candidates, in any order
assert_completions() {
    assert_equal "$(printf '%s\n' "${COMPREPLY[@]}" | sort)" "$(printf '%s\n' "$@" | sort)"
}

assert_no_completions() {
    assert_equal "${#COMPREPLY[@]}" 0
}

# assert_completion_contains CANDIDATE - COMPREPLY contains at least this candidate
assert_completion_contains() {
    local c
    for c in "${COMPREPLY[@]}"; do
        [[ $c == "$1" ]] && return 0
    done
    fail "'$1' not in completions: ${COMPREPLY[*]}"
}

assert_called() {
    assert_file_contains_line "$STUB_LOG" "$1"
}

assert_file_contains_line() {
    grep -qxF -- "$2" "$1" || fail "expected call '$2', got: $(cat "$1" 2>/dev/null)"
}

# assert_no_leaked_globals CMD WORD... - completing must not create new shell variables
assert_no_leaked_globals() {
    local before="" after=""
    before=$(compgen -v | sort)
    complete_words "$@"
    after=$(compgen -v | sort)
    assert_equal "$(comm -13 <(echo "$before") <(echo "$after"))" ""
}

# assert_no_stderr CMD WORD... - completing must stay silent even if the ws_* tools fail
assert_no_stderr() {
    STUB_FAIL=1 complete_words "$@" 2> "$BATS_TEST_TMPDIR/stderr"
    assert_equal "$(cat "$BATS_TEST_TMPDIR/stderr")" ""
}



@test "completion: loading prints nothing" {
    for cmd in "${COMPLETION_TOOLS[@]}"; do
        run load_completion "$cmd"
        assert_success
        assert_output ""
    done
}

@test "completion: each file registers the completion for its tool" {
    for cmd in "${COMPLETION_TOOLS[@]}"; do
        load_completion "$cmd"
        complete -p "$cmd"
    done
}

@test "completion: each file can be loaded on its own" {
    for cmd in "${COMPLETION_TOOLS[@]}"; do
        run bash -c 'source "$1" && complete -p "$2"' _ "$COMPLETION_DIR/completions/$cmd.bash" "$cmd"
        assert_success
    done
}

@test "completion: the helpers are not sourced again if already loaded" {
    load_completion ws_find
    _ws_filesystem_list() { echo marker; }
    load_completion ws_list
    assert_equal "$(_ws_filesystem_list)" marker
}

@test "completion: no files without a tool test" {
    local f
    for f in "$COMPLETION_DIR"/completions/*; do
        f=${f##*/}
        [[ " ${COMPLETION_TOOLS[*]} " == *" ${f%.bash} "* ]] || fail "no tests for completion file $f"
    done
}

@test "completion: all files are installed" {
    local f
    for f in "$COMPLETION_DIR"/completions/* "$COMPLETION_DIR"/helpers/*; do
        f=${f#"$PROJECT_ROOT/"}
        grep -qF "$f" "$PROJECT_ROOT/CMakeLists.txt" || fail "$f is not installed in CMakeLists.txt"
    done
}

@test "completion: the bash-completion loader finds the files" {
    local bash_completion=/usr/share/bash-completion/bash_completion
    [[ -r $bash_completion ]] || skip "bash-completion not installed"
    # the source tree has the same layout as the install directory, so it can be used as user dir
    run env BASH_COMPLETION_USER_DIR="$COMPLETION_DIR" bash --norc --noprofile -c '
        source "$1" || exit 1
        loader=$(complete -p -D | sed -E "s/.* -F ([^ ]+) .*/\1/")
        for cmd in "${@:2}"; do
            "$loader" "$cmd"
            complete -p "$cmd" || exit 1
        done' _ "$bash_completion" "${COMPLETION_TOOLS[@]}"
    assert_success
    for cmd in "${COMPLETION_TOOLS[@]}"; do
        assert_line "complete -F _complete_${cmd} ${cmd}"
    done
}

@test "completion: works in a shell with set -u" {
    # the completion functions run in the user's shell, which may have set -u (nounset)
    run bash -c '
        set -u
        for f in "$1"/completions/*.bash; do source "$f"; done
        c() { COMP_WORDS=("$@"); COMP_CWORD=$(( $# - 1 )); COMPREPLY=(); "_complete_$1"; }
        c ws_allocate -x -F ws1 ""
        c ws_extend -F ws1 ""
        c ws_find -g -F ws1 ""
        c ws_list -F ""
        c ws_release -F ws1 ""
        c ws_restore -F ws1 user-alpha-1700000000 ""
        c ws_share -F ws1 share ""
        c ws_share unshare alpha ""
        echo done' _ "$COMPLETION_DIR"
    assert_success
    assert_output "done"
}

# --- ws_find ---

@test "ws_find completion: options" {
    complete_words ws_find -
    assert_completions -F --filesystem -g --group -h --help -n --name -u --username -V --version
}

@test "ws_find completion: long options by prefix" {
    complete_words ws_find --
    assert_completions --filesystem --group --help --name --username --version
}

@test "ws_find completion: workspace names of all filesystems" {
    complete_words ws_find ""
    assert_completions alpha beta gamma
    assert_called "ws_list -s"
}

@test "ws_find completion: workspace names by prefix" {
    complete_words ws_find a
    assert_completions alpha
}

@test "ws_find completion: filesystems after -F" {
    complete_words ws_find -F ""
    assert_completions ws1 ws2
}

@test "ws_find completion: filesystems after --filesystem" {
    complete_words ws_find --filesystem ""
    assert_completions ws1 ws2
}

@test "ws_find completion: workspace names restricted by -F" {
    complete_words ws_find -F ws1 ""
    assert_completions alpha beta
    assert_called "ws_list -F ws1 -s"
}

@test "ws_find completion: only one workspace name" {
    complete_words ws_find alpha ""
    assert_no_completions
}

@test "ws_find completion: -F of a previous completion is forgotten" {
    complete_words ws_find -F ws2 ""
    complete_words ws_find ""
    assert_completions alpha beta gamma
}

@test "ws_find completion: no leaked global variables" {
    assert_no_leaked_globals ws_find -F ws1 ""
}

@test "ws_find completion: no output on stderr" {
    assert_no_stderr ws_find ""
}

# --- ws_list ---

@test "ws_list completion: options" {
    complete_words ws_list -
    assert_completions -h --help -V --version -F --filesystem -g --group -l --listfilesystems -L --listfilesystemdetails -s --short -u --username -e --expired -N --name -C --creation -R --remaining -r --reverted -t --terse -T --table -p --pattern -P --permissions -v --verbose -n --threads
}

@test "ws_list completion: options without prefix" {
    complete_words ws_list ""
    assert_completion_contains --filesystem
}

@test "ws_list completion: filesystems after -F" {
    complete_words ws_list -F ""
    assert_completions ws1 ws2
}

@test "ws_list completion: filesystems after --filesystem" {
    complete_words ws_list --filesystem ""
    assert_completions ws1 ws2
}

@test "ws_list completion: users after -u" {
    complete_words ws_list -u ro
    assert_completion_contains root
}

@test "ws_list completion: no leaked global variables" {
    assert_no_leaked_globals ws_list -F ""
}

@test "ws_list completion: no output on stderr" {
    assert_no_stderr ws_list -F ""
}

# --- ws_extend ---

@test "ws_extend completion: workspace names" {
    complete_words ws_extend ""
    assert_completions alpha beta gamma
}

@test "ws_extend completion: workspace names by prefix" {
    complete_words ws_extend g
    assert_completions gamma
}

@test "ws_extend completion: no leaked global variables" {
    assert_no_leaked_globals ws_extend ""
}

@test "ws_extend completion: no output on stderr" {
    assert_no_stderr ws_extend ""
}

# --- ws_release ---

@test "ws_release completion: options" {
    complete_words ws_release -
    assert_completions -F --filesystem -h --help -n --name -u --username -V --version --delete-data
}

@test "ws_release completion: filesystems after -F" {
    complete_words ws_release -F ""
    assert_completions ws1 ws2
}

@test "ws_release completion: workspace names" {
    complete_words ws_release ""
    assert_completions alpha beta gamma
}

@test "ws_release completion: workspace names restricted by -F" {
    complete_words ws_release -F ws2 ""
    assert_completions gamma
}

@test "ws_release completion: no leaked global variables" {
    assert_no_leaked_globals ws_release ""
}

@test "ws_release completion: no output on stderr" {
    assert_no_stderr ws_release ""
}

# --- ws_restore ---

@test "ws_restore completion: options" {
    complete_words ws_restore -
    assert_completions -b --brief --delete-data -F --filesystem -h --help -l --list -n --name -t --target -u --username -V --version
}

@test "ws_restore completion: restorable workspaces" {
    complete_words ws_restore ""
    assert_completions user-alpha-1700000000 user-delta-1700000001
    assert_called "ws_restore -l -b"
}

@test "ws_restore completion: filesystems after -F" {
    complete_words ws_restore -F ""
    assert_completions ws1 ws2
}

@test "ws_restore completion: target workspace after restorable workspace" {
    complete_words ws_restore user-alpha-1700000000 ""
    assert_completions alpha beta gamma
}

@test "ws_restore completion: restorable workspaces after --name" {
    complete_words ws_restore --name ""
    assert_completions user-alpha-1700000000 user-delta-1700000001
}

@test "ws_restore completion: target workspace after --target" {
    complete_words ws_restore --target ""
    assert_completions alpha beta gamma
}

@test "ws_restore completion: no leaked global variables" {
    assert_no_leaked_globals ws_restore ""
}

@test "ws_restore completion: no output on stderr" {
    assert_no_stderr ws_restore ""
    assert_no_stderr ws_restore --name ""
    assert_no_stderr ws_restore --target ""
    assert_no_stderr ws_restore user-alpha-1700000000 ""
}

# --- ws_share ---

@test "ws_share completion: actions" {
    complete_words ws_share ""
    assert_completions share unshare unshare-all unsharegroup sharegroup list
}

@test "ws_share completion: options" {
    complete_words ws_share -
    assert_completions -F --filesystem -h --help --readwrite --version
}

@test "ws_share completion: share completes workspace names" {
    complete_words ws_share share ""
    assert_completions alpha beta gamma
}

@test "ws_share completion: share completes workspace names restricted by -F" {
    complete_words ws_share share -F ws1 ""
    assert_completions alpha beta
    assert_called "ws_list -F ws1 -s"
}

@test "ws_share completion: filesystems after -F" {
    complete_words ws_share share -F ""
    assert_completions ws1 ws2
}

@test "ws_share completion: share completes users after workspace name" {
    complete_words ws_share share alpha ro
    assert_completion_contains root
}

@test "ws_share completion: unshare completes workspace names" {
    complete_words ws_share unshare ""
    assert_completions alpha beta gamma
}

@test "ws_share completion: unshare queries the current shares" {
    complete_words ws_share unshare alpha ""
    assert_called "ws_share list alpha"
}

@test "ws_share completion: unshare queries the current shares restricted by -F" {
    complete_words ws_share unshare -F ws1 alpha ""
    assert_called "ws_share list -F ws1 alpha"
}

@test "ws_share completion: unshare completes users the workspace is shared with" {
    complete_words ws_share unshare alpha ""
    assert_completions alice
}

@test "ws_share completion: list completes workspace names" {
    complete_words ws_share list ""
    assert_completions alpha beta gamma
}

@test "ws_share completion: unshare-all completes workspace names" {
    complete_words ws_share unshare-all ""
    assert_completions alpha beta gamma
}

@test "ws_share completion: sharegroup completes workspace names" {
    complete_words ws_share sharegroup ""
    assert_completions alpha beta gamma
}

@test "ws_share completion: nothing after workspace name for list" {
    complete_words ws_share list alpha ""
    assert_no_completions
}

@test "ws_share completion: no leaked global variables" {
    assert_no_leaked_globals ws_share unshare -F ws1 alpha ""
}

@test "ws_share completion: no output on stderr" {
    assert_no_stderr ws_share unshare alpha ""
}

# --- new tests: -F handling via _ws_option_filesystem and _ws_complete_filesystem ---

option_filesystem() {
    complete -p ws_find &>/dev/null || load_completion ws_find
    COMP_WORDS=("$@")
    COMP_CWORD=$(( ${#COMP_WORDS[@]} - 1 ))
    _ws_option_filesystem
}

@test "_ws_option_filesystem: forms of -F" {
    assert_equal "$(option_filesystem ws_x "")" ""
    assert_equal "$(option_filesystem ws_x -F ws1 "")" ws1
    assert_equal "$(option_filesystem ws_x -Fws1 "")" ws1
    assert_equal "$(option_filesystem ws_x --filesystem ws1 "")" ws1
    assert_equal "$(option_filesystem ws_x alpha -F ws2 "")" ws2
    assert_equal "$(option_filesystem ws_x -u bob -F ws2 alpha "")" ws2
}

@test "_ws_option_filesystem: value being completed is not used" {
    assert_equal "$(option_filesystem ws_x -F ws)" ""
}

@test "filesystem value completion: all forms, all tools" {
    local cmd
    for cmd in ws_find ws_list ws_extend ws_release ws_restore ws_share; do
        local pre=()
        [[ $cmd == ws_share ]] && pre=(share)
        complete_words $cmd "${pre[@]}" -F ""
        assert_completions ws1 ws2
        complete_words $cmd "${pre[@]}" --filesystem w
        assert_completions ws1 ws2
        complete_words $cmd "${pre[@]}" -Fw
        assert_completions -Fws1 -Fws2
    done
}

@test "filesystem value completion: -F alone still completes as option" {
    complete_words ws_release -F
    assert_completions -F
}

@test "ws_extend completion: workspace names restricted by -F" {
    complete_words ws_extend -F ws1 ""
    assert_completions alpha beta
    assert_called "ws_list -F ws1 -s"
}

@test "ws_extend completion: no output on stderr with -F" {
    assert_no_stderr ws_extend -F ws1 ""
}

@test "ws_release completion: workspace names restricted by -F after other options" {
    complete_words ws_release -u bob -F ws2 ""
    assert_completions gamma
}

@test "ws_restore completion: restorable workspaces restricted by -F" {
    complete_words ws_restore -F ws1 ""
    assert_completions user-alpha-1700000000
    assert_called "ws_restore -F ws1 -l -b"
}

@test "ws_restore completion: target workspaces restricted by -F" {
    complete_words ws_restore -F ws1 --target ""
    assert_completions alpha beta
    assert_called "ws_list -F ws1 -s"
}

@test "ws_restore completion: no leaked global variables with -F" {
    assert_no_leaked_globals ws_restore -F ws1 ""
}

# --- new tests: ws_restore name and target ---

@test "ws_restore completion: restorable workspaces after -n" {
    complete_words ws_restore -n ""
    assert_completions user-alpha-1700000000 user-delta-1700000001
}

@test "ws_restore completion: target workspace after -t" {
    complete_words ws_restore -t ""
    assert_completions alpha beta gamma
}

@test "ws_restore completion: target workspace after second of several restorable workspaces" {
    complete_words ws_restore user-delta-1700000001 ""
    assert_completions alpha beta gamma
}

@test "ws_restore completion: target workspace after --name ID" {
    complete_words ws_restore --name user-alpha-1700000000 ""
    assert_completions alpha beta gamma
}

@test "ws_restore completion: restorable workspace after --target" {
    complete_words ws_restore --target alpha ""
    assert_completions user-alpha-1700000000 user-delta-1700000001
}

@test "ws_restore completion: no target after --delete-data ID" {
    complete_words ws_restore --delete-data user-alpha-1700000000 ""
    assert_no_completions
}

@test "ws_restore completion: restorable workspaces after --delete-data" {
    complete_words ws_restore --delete-data ""
    assert_completions user-alpha-1700000000 user-delta-1700000001
}

@test "ws_restore completion: --target does not list restorable workspaces" {
    complete_words ws_restore --target ""
    run grep -c "^ws_restore " "$STUB_LOG"
    assert_output 0
}

@test "ws_restore completion: prefix of an ID is not taken as ID" {
    complete_words ws_restore user-alpha ""
    assert_completions user-alpha-1700000000 user-delta-1700000001
}

# --- new tests: ws_share ---

@test "ws_share completion: unshare completes only shared users, not groups" {
    complete_words ws_share unshare alpha s
    assert_no_completions
}

@test "ws_share completion: unshare completes shared users by prefix" {
    complete_words ws_share unshare -F ws1 alpha a
    assert_completions alice
    assert_called "ws_share list -F ws1 alpha"
}

@test "ws_share completion: unshare completes further shared users" {
    complete_words ws_share unshare alpha alice ""
    assert_completions alice
}

@test "ws_share completion: nothing after workspace name for unshare-all" {
    complete_words ws_share unshare-all alpha ""
    assert_no_completions
}

@test "ws_share completion: nothing after workspace name for sharegroup" {
    complete_words ws_share sharegroup alpha ""
    assert_no_completions
}

@test "ws_share completion: nothing on stderr for unshare if ws_share fails" {
    assert_no_stderr ws_share unshare alpha ""
}

# --- new tests: ws_share argument scanning ---

@test "ws_share completion: options before the action" {
    complete_words ws_share -F ws1 ""
    assert_completions share unshare unshare-all unsharegroup sharegroup list
    complete_words ws_share --readwrite ""
    assert_completions share unshare unshare-all unsharegroup sharegroup list
    complete_words ws_share --config my.conf ""
    assert_completions share unshare unshare-all unsharegroup sharegroup list
}

@test "ws_share completion: -F before the action restricts workspace names" {
    complete_words ws_share -F ws1 share ""
    assert_completions alpha beta
}

@test "ws_share completion: --readwrite and --config before the action" {
    complete_words ws_share --readwrite --config my.conf share ""
    assert_completions alpha beta gamma
    complete_words ws_share --readwrite share alpha ro
    assert_completion_contains root
}

@test "ws_share completion: -F after the workspace name" {
    complete_words ws_share unshare alpha -F ws1 ""
    assert_completions alice
    assert_called "ws_share list -F ws1 alpha"
}

@test "ws_share completion: -Ffs restricts workspace names" {
    complete_words ws_share share -Fws2 ""
    assert_completions gamma
}

@test "ws_share completion: unshare with options before the action queries the right workspace" {
    complete_words ws_share -F ws1 unshare alpha ""
    assert_called "ws_share list -F ws1 alpha"
}

@test "ws_share completion: no leaked global variables with options" {
    assert_no_leaked_globals ws_share --readwrite -F ws1 unshare alpha ""
}

# --- new tests: ws_find argument scanning ---

@test "ws_find completion: options after other options and after the name" {
    complete_words ws_find -g --
    assert_completions --filesystem --group --help --name --username --version
    complete_words ws_find alpha --h
    assert_completions --help
}

@test "ws_find completion: workspace names after flags" {
    complete_words ws_find -g ""
    assert_completions alpha beta gamma
}

@test "ws_find completion: workspace names restricted by -F after other options" {
    complete_words ws_find -g -F ws1 ""
    assert_completions alpha beta
    assert_called "ws_list -F ws1 -s"
}

@test "ws_find completion: -Ffs restricts workspace names" {
    complete_words ws_find -Fws2 ""
    assert_completions gamma
}

@test "ws_find completion: workspace names after -n" {
    complete_words ws_find -F ws1 -n ""
    assert_completions alpha beta
    complete_words ws_find --name g
    assert_completions gamma
}

@test "ws_find completion: no workspace name as value of -u and --config" {
    complete_words ws_find -u ""
    assert_no_completions
    complete_words ws_find --config ""
    assert_no_completions
}

@test "ws_find completion: workspace names after -u and --config values" {
    complete_words ws_find -u bob ""
    assert_completions alpha beta gamma
    complete_words ws_find --config my.conf -F ws2 ""
    assert_completions gamma
}

@test "ws_find completion: nothing after a name given with -n" {
    complete_words ws_find -n alpha ""
    assert_no_completions
}

@test "ws_find completion: nothing after the name and options" {
    complete_words ws_find alpha -F ws1 ""
    assert_no_completions
}

@test "ws_find completion: no leaked global variables with options" {
    assert_no_leaked_globals ws_find -g -F ws1 ""
}

# --- new tests: ws_list, ws_extend options ---

@test "ws_list completion: users after --username" {
    complete_words ws_list --username ro
    assert_completion_contains root
}

@test "ws_list completion: nothing after options with a value" {
    local opt
    for opt in -p --pattern -n --threads --config; do
        complete_words ws_list $opt ""
        assert_no_completions
    done
}

@test "ws_list completion: no option -a" {
    complete_words ws_list -a
    assert_no_completions
}

@test "ws_extend completion: -F as first argument" {
    complete_words ws_extend -
    assert_completions -F
}

@test "ws_extend completion: no options after the first argument" {
    complete_words ws_extend alpha -
    assert_no_completions
}

# --- new tests: ws_allocate ---

@test "ws_allocate completion: options" {
    complete_words ws_allocate -
    assert_completions -h --help -V --version -d --duration -n --name -F --filesystem -r --reminder -m --mailaddress -x --extension -u --username -g --groupreadable -G --groupwritable -c --comment
}

@test "ws_allocate completion: no name for a new workspace" {
    complete_words ws_allocate ""
    assert_no_completions
    complete_words ws_allocate -n ""
    assert_no_completions
    complete_words ws_allocate -F ws1 ""
    assert_no_completions
}

@test "ws_allocate completion: existing workspace names with -x" {
    complete_words ws_allocate -x ""
    assert_completions alpha beta gamma
    complete_words ws_allocate --extension -F ws1 ""
    assert_completions alpha beta
    complete_words ws_allocate -x -n ""
    assert_completions alpha beta gamma
}

@test "ws_allocate completion: -x after the cursor counts as well" {
    COMP_WORDS=(ws_allocate "" -x) COMP_CWORD=1 COMPREPLY=()
    complete -p ws_allocate &>/dev/null || load_completion ws_allocate
    _complete_ws_allocate
    assert_completions alpha beta gamma
}

@test "ws_allocate completion: no duration" {
    complete_words ws_allocate -x alpha ""
    assert_no_completions
    complete_words ws_allocate -x -n alpha ""
    assert_no_completions
    complete_words ws_allocate -x -d ""
    assert_no_completions
}

@test "ws_allocate completion: values of options are no workspace names" {
    complete_words ws_allocate -x -m me@example.org -r 3 -c "a comment" ""
    assert_completions alpha beta gamma
    complete_words ws_allocate -x -g ""
    assert_completions alpha beta gamma
}

@test "ws_allocate completion: filesystems after -F" {
    complete_words ws_allocate -F ""
    assert_completions ws1 ws2
}

@test "ws_allocate completion: users after -u" {
    complete_words ws_allocate -u ro
    assert_completion_contains root
}

@test "ws_allocate completion: nothing after options with a value" {
    local opt
    for opt in -d --duration -r --reminder -m --mailaddress -c --comment --config; do
        complete_words ws_allocate -x $opt ""
        assert_no_completions
    done
}

@test "ws_allocate completion: no leaked global variables" {
    assert_no_leaked_globals ws_allocate -x -F ws1 ""
}

@test "ws_allocate completion: no output on stderr" {
    assert_no_stderr ws_allocate -x ""
}

# --- new tests: --config is not offered, but its value is skipped ---

@test "completion: --config is not offered" {
    local cmd
    for cmd in ws_allocate ws_find ws_list ws_release ws_restore ws_share; do
        complete_words $cmd --conf
        assert_no_completions
    done
}
