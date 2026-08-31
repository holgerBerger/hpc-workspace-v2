#!/bin/bash
#
# ws_function_test.sh — self-contained functional smoke test for the ws_*
# command suite. Calls the tools and checks their outcomes.
#
# Usage: ws_function_test.sh
#
# known issues: reads only /etc/ws.conf, not /etc/ws.d/*
#
# Exit code: 0 if all checks passed, 1 otherwise.
#

# --- colors (disabled when stdout is not a terminal) -------------------------
if [[ -t 1 ]]; then
    GREEN=$'\033[32m'
    RED=$'\033[31m'
    RESET=$'\033[0m'
else
    GREEN=''
    RED=''
    RESET=''
fi

PASS=0
FAIL=0

# check DESCRIPTION
# Report the outcome of the *last* command: green check if it succeeded,
# red cross otherwise. Must be called immediately after the command so that
# `$?` still holds its exit status:
#
#   ws_list --version
#   check "ws_list --version"
#
check() {
    local rc=$?                 # must be the very first statement
    local desc="${1:-}"
    if [[ $rc -eq 0 ]]; then
        printf '%s✔%s %s\n' "$GREEN" "$RESET" "$desc"
        PASS=$((PASS + 1))
    else
        printf '%s✘%s %s (rc=%d)\n' "$RED" "$RESET" "$desc" "$rc"
        FAIL=$((FAIL + 1))
    fi
}

###############################################################################
#
# source: https://github.com/mrbaseman/parse_yaml.git
#
###############################################################################
# Parses a YAML file ('-' means standard input), or standard input if file is
# not given, and outputs variable assigments.  Can optionally accept a variable
# name prefix and a variable name separator if file is given.
#
# Usage:
#   parse_yaml
#   parse_yaml file|- [prefix] [separator]
###############################################################################

function parse_yaml {
    unset i
    unset fs
    local prefix=$2
    local separator=${3:-_}

    local indexfix=-1
    # Detect awk flavor
    if awk --version 2>&1 | grep -q "GNU Awk" ; then
        # GNU Awk detected
        indexfix=-1
    elif awk -Wv 2>&1 | grep -q "mawk" ; then
        # mawk detected
        indexfix=0
    fi

    local s='[[:space:]]*' sm='[ \t]*' w='[a-zA-Z0-9_.]*' fs=${fs:-$(echo @|tr @ '\034')} i=${i:-  }

    ###############################################################################
    # cat:   read the yaml file (or stdin) into the stream
    # awk 1: process multi-line text
    # sed 1: remove comments and empty lines
    # sed 2: process lists
    # sed 3: process dictionaries
    # sed 4: rearrange anchors
    # sed 5: remove '---'/'...'/quotes, add file separator to create fields for awk 2
    # awk 2: convert the formatted data to variable assignments
    ###############################################################################

    echo | cat ${1:--} - | \
    awk -F$fs "{multi=0;
        if(match(\$0,/$sm\|$sm$/)){multi=1; sub(/$sm\|$sm$/,\"\");}
        if(match(\$0,/$sm>$sm$/)){multi=2; sub(/$sm>$sm$/,\"\");}
        while(multi>0){
            str=\$0; gsub(/^$sm/,\"\", str);
            indent=index(\$0,str);
            indentstr=substr(\$0, 0, indent+$indexfix) \"$i\";
            obuf=\$0;
            getline;
            while(index(\$0,indentstr)){
                obuf=obuf substr(\$0, length(indentstr)+1);
                if (multi==1){obuf=obuf \"\\\\n\";}
                if (multi==2){
                    if(match(\$0,/^$sm$/))
                        obuf=obuf \"\\\\n\";
                        else obuf=obuf \" \";
                }
                getline;
            }
            sub(/$sm$/,\"\",obuf);
            print obuf;
            multi=0;
            if(match(\$0,/$sm\|$sm$/)){multi=1; sub(/$sm\|$sm$/,\"\");}
            if(match(\$0,/$sm>$sm$/)){multi=2; sub(/$sm>$sm$/,\"\");}
        }
    print}" | \
    sed -e "s|^\($s\)?|\1-|" \
        -ne "s|^\($s\)-$s\($w\)$s:$s\(.*\)|\1-\n\1 \2: \3|" \
        -ne "s|^$s#.*||;s|$s#[^\"']*$||;s|^\([^\"'#]*\)#.*|\1|;t 1" \
        -ne "t" \
        -ne ":1" \
        -ne "s|^$s\$||;t 2" \
        -ne "p" \
        -ne ":2" \
        -ne "d" | \
    sed -ne "s|,$s\]|]|g" \
        -e ":1" \
        -e "s|^\($s\)\($w\)$s:$s\(&$w\)$s\[$s\(.*\)$s,$s\(.*\)$s\]|\1\2: \3[\4]\n\1$i- \5|;t 1" \
        -e "s|^\($s\)\($w\)$s:$s\(&$w\)$s\[$s\(.*\)$s\]|\1\2: \3\n\1$i- \4|;" \
        -e ":2" \
        -e "s|^\($s\)\($w\)$s:$s\[$s\(.*\)$s,$s\(.*\)$s\]|\1\2: [\3]\n\1$i- \4|;t 2" \
        -e "s|^\($s\)\($w\)$s:$s\[$s\(.*\)$s\]|\1\2:\n\1$i- \3|;" \
        -e ":3" \
        -e "s|^\($s\)-$s\[$s\(.*\)$s,$s\(.*\)$s\]|\1- [\2]\n\1$i- \3|;t 3" \
        -e "s|^\($s\)-$s\[$s\(.*\)$s\]|\1-\n\1$i- \2|;p" | \
    sed -ne "s|,$s}|}|g" \
        -e ":1" \
        -e "s|^\($s\)-$s{$s\(.*\)$s,$s\($w\)$s:$s\(.*\)$s}|\1- {\2}\n\1$i\3: \4|;t 1" \
        -e "s|^\($s\)-$s{$s\(.*\)$s}|\1-\n\1$i\2|;" \
        -e ":2" \
        -e "s|^\($s\)\($w\)$s:$s\(&$w\)$s{$s\(.*\)$s,$s\($w\)$s:$s\(.*\)$s}|\1\2: \3 {\4}\n\1$i\5: \6|;t 2" \
        -e "s|^\($s\)\($w\)$s:$s\(&$w\)$s{$s\(.*\)$s}|\1\2: \3\n\1$i\4|;" \
        -e ":3" \
        -e "s|^\($s\)\($w\)$s:$s{$s\(.*\)$s,$s\($w\)$s:$s\(.*\)$s}|\1\2: {\3}\n\1$i\4: \5|;t 3" \
        -e "s|^\($s\)\($w\)$s:$s{$s\(.*\)$s}|\1\2:\n\1$i\3|;p" | \
    sed -e "s|^\($s\)\($w\)$s:$s\(&$w\)\(.*\)|\1\2:\4\n\3|" \
        -e "s|^\($s\)-$s\(&$w\)\(.*\)|\1- \3\n\2|" | \
    sed -ne "s|^\($s\):|\1|" \
        -e "s|^\($s\)\(---\)\($s\)||" \
        -e "s|^\($s\)\(\.\.\.\)\($s\)||" \
        -e "s|^\($s\)-${s}[\"']\(.*\)[\"']$s\$|\1$fs$fs\2|p;t" \
        -e "s|^\($s\)\($w\)$s:${s}[\"']\(.*\)[\"']$s\$|\1$fs\2$fs\3|p;t" \
        -e "s|^\($s\)-$s\(.*\)$s\$|\1$fs$fs\2|" \
        -e "s|^\($s\)\($w\)$s:${s}[\"']\?\(.*\)$s\$|\1$fs\2$fs\3|" \
        -e "s|^\($s\)[\"']\?\([^&][^$fs]\+\)[\"']$s\$|\1$fs$fs$fs\2|" \
        -e "s|^\($s\)[\"']\?\([^&][^$fs]\+\)$s\$|\1$fs$fs$fs\2|" \
        -e "s|^\($s\)\($w\)$s:${s}[\"']\(.*\)$s\$|\1$fs\2$fs\3|" \
        -e "s|^\($s\)[\"']\([^&][^$fs]*\)[\"']$s\$|\1$fs$fs$fs\2|" \
        -e "s|^\($s\)[\"']\([^&][^$fs]*\)$s\$|\1$fs$fs$fs\2|" \
        -e "s|^\($s\)\($w\)$s:$s\(.*\)$s\$|\1$fs\2$fs\3|" \
        -e "s|^\($s\)\([^&][^$fs]*\)[\"']$s\$|\1$fs$fs$fs\2|" \
        -e "s|^\($s\)\([^&][^$fs]*\)$s\$|\1$fs$fs$fs\2|" \
        -e "s|$s\$||p" | \
    awk -F$fs "{
        gsub(/\t/,\"        \",\$1);
        if(NF>3){if(value!=\"\"){value = value \" \";}value = value  \$4;}
        else {
            if(match(\$1,/^&/)){anchor[substr(\$1,2)]=full_vn;getline};
            indent = length(\$1)/length(\"$i\");
            vname[indent] = \$2;
            value= \$3;
            for (i in vname) {if (i > indent) {delete vname[i]; idx[i]=0}}
            if(length(\$2)== 0){  vname[indent]= ++idx[indent] };
            vn=\"\"; for (i=0; i<indent; i++) { vn=(vn)(vname[i])(\"$separator\")}
            vn=\"$prefix\" vn;
            full_vn=vn vname[indent];
            if(vn==\"$prefix\")vn=\"$prefix$separator\";
            if(vn==\"_\")vn=\"__\";
        }
        gsub(/\./,\"$separator\",full_vn);
        gsub(/\\\\\"/,\"\\\"\",value);
        gsub(/'/,\"'\\\"'\\\"'\",value);
        assignment[full_vn]=value;
        if(!match(assignment[vn], full_vn))assignment[vn]=assignment[vn] \" \" full_vn;
        if(match(value,/^\*/)){
            ref=anchor[substr(value,2)];
            if(length(ref)==0){
                printf(\"%s='%s'\n\", full_vn, value);
            } else {
                for(val in assignment){
                    if((length(ref)>0)&&index(val, ref)==1){
                        tmpval=assignment[val];
                        sub(ref,full_vn,val);
                        if(match(val,\"$separator\$\")){
                            gsub(ref,full_vn,tmpval);
                        } else if (length(tmpval) > 0) {
                            printf(\"%s='%s'\n\", val, tmpval);
                        }
                        assignment[val]=tmpval;
                    }
                }
            }
        } else if (length(value) > 0) {
            printf(\"%s='%s'\n\", full_vn, value);
        }
    }END{
        for(val in assignment){
            if(match(val,\"$separator\$\"))
                printf(\"%s='%s'\n\", val, assignment[val]);
        }
    }"
}

# ws_allocate
ws_allocate TESTWORKSPACE 1  2>/dev/null >/dev/null
check "ws_allocate TESTWORKSPACE 1"
FILESYSTEM=$(ws_list TESTWORKSPACE | grep "filesystem name" | awk '{print $4}')
eval $(parse_yaml /etc/ws.conf)
db=$(eval echo \$workspaces_${FILESYSTEM}_database)
dbfile="$db"/$USER-TESTWORKSPACE
[ -f $dbfile ] ; check "  db found"
[ $(stat -c '%u' $dbfile) -eq $dbuid ] ; check "  db owner is correct"
DIRECTORY=$(ws_list TESTWORKSPACE | grep "workspace directory" | awk '{print $4}')
[ -d $DIRECTORY ] ; check "  workspace directory found"
[ $(stat -c '%u' $DIRECTORY) -eq $(id -u) ] ; check "  workspace owner is correct"
touch $DIRECTORY/TESTFILE; check "  TESTFILE created"

# ws_release
ws_release TESTWORKSPACE  2>/dev/null >/dev/null; check "ws_release TESTWORKSPACE"
[ ! -d $DIRECTORY ] ; check "  workspace removed"

# ws_restore
ws_restore -l TESTWORKSPACE*  2>/dev/null >/dev/null; check "ws_restore -l TESTWORKSPACE*"
ID=$(ws_restore -l TESTWORKSPACE* | head -1)
TARGET=$(ws_allocate RESTORETARGET 1  2>/dev/null)
ws_restore $ID RESTORETARGET
[ -f ${TARGET}/*/TESTFILE ] ; check "  TESTFILE restored"

# ws_release --delete-data
ws_release --delete-data RESTORETARGET
[ ! -f ${TARGET}/*/TESTFILE ] ; check "  TESTFILE removed"


# --- summary -----------------------------------------------------------------
echo
echo "passed: $PASS, failed: $FAIL"
if [[ $FAIL -gt 0 ]]; then
    exit 1
fi
exit 0
