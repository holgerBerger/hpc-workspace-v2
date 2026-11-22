#!/bin/bash

_common_setup() {
    bats_load_library 'bats-support'
    bats_load_library 'bats-assert'
    bats_load_library 'bats-file'

    # get the containing directory of this file
    # use $BATS_TEST_FILENAME instead of ${BASH_SOURCE[0]} or $0,
    # as those will point to the bats executable's location or the preprocessed file respectively
    PROJECT_ROOT="$( cd "$( dirname "$BATS_TEST_FILENAME" )/.." >/dev/null 2>&1 && pwd )"
    # make executables in src/ visible to PATH
    PATH="/tmp/setuid:$PROJECT_ROOT/../build/${PRESET:=debug}/bin/:$PATH"

    mkdir -p /tmp/ws/ws1-db
    mkdir -p /tmp/ws/ws1-db/.removed
    mkdir -p /tmp/ws/ws2-db
    mkdir -p /tmp/ws/ws2-db/.removed
    mkdir -p /tmp/ws/ws1
    mkdir -p /tmp/ws/ws1/.removed
    mkdir -p /tmp/ws/ws2/1
    mkdir -p /tmp/ws/ws2/1/.removed
    mkdir -p /tmp/ws/ws2/2
    mkdir -p /tmp/ws/ws2/2/.removed

    if [ ! -e /tmp/ws/ws1-db/.ws_db_magic ]
    then
        echo ws1  > /tmp/ws/ws1-db/.ws_db_magic
        echo ws2  > /tmp/ws/ws2-db/.ws_db_magic
    fi

    # In non-login contexts (containers, CI) bash does not set USER, and the
    # /.dockerenv marker is Docker-only: podman writes /run/.containerenv
    # instead. Derive the name from the real uid so the tests work everywhere
    # and follow the configured test user instead of hardcoding usera.
    if [ -z "${USER:-}" ]
    then
        USER=$(id -un)
    fi
    export USER
}
