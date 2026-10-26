#!/usr/bin/env python3
"""
ws_function_test.py — Self-contained functional smoke test for the ws_* command suite.
Calls the tools and checks their outcomes.

Exit code: 0 if all checks passed, 1 otherwise.
"""

import os
import sys
import glob
import subprocess
import yaml

# --- Colors (disabled when stdout is not a TTY) -----------------------------
if sys.stdout.isatty():
    GREEN = "\033[32m"
    RED = "\033[31m"
    RESET = "\033[0m"
else:
    GREEN = ""
    RED = ""
    RESET = ""

PASS = 0
FAIL = 0


def check(desc: str, rc: int) -> None:
    """Report outcome of a test command."""
    global PASS, FAIL
    if rc == 0:
        print(f"{GREEN}✔{RESET} {desc}")
        PASS += 1
    else:
        print(f"{RED}✘{RESET} {desc} (rc={rc})")
        FAIL += 1


def run_cmd(cmd_list, capture_output=True, interactive=False):
    """
    Utility to run shell commands cleanly.
    Set interactive=True to allow TTY prompt interactions directly on the terminal.
    """
    if interactive:
        return subprocess.run(
            cmd_list,
            stdin=sys.stdin,
            stdout=sys.stdout,
            stderr=sys.stderr,
            shell=isinstance(cmd_list, str)
        )

    # Python 3.6 compatible equivalents for capture_output=True and text=True
    stdout_val = subprocess.PIPE if capture_output else None
    stderr_val = subprocess.PIPE if capture_output else None

    return subprocess.run(
        cmd,
        universal_newlines=True,
        stdout=stdout_val,
        stderr=stderr_val,
        shell=use_shell
    )

def merge_yaml(base: dict, extra: dict) -> dict:
    """Recursively merge `extra` into `base`; later values win for scalar keys."""
    for key, value in extra.items():
        if isinstance(value, dict) and isinstance(base.get(key), dict):
            merge_yaml(base[key], value)
        else:
            base[key] = value
    return base


def load_config(conf_dir: str = "/etc/ws.d", conf_file: str = "/etc/ws.conf") -> dict:
    """
    Load configuration via PyYAML, mirroring the C++ Config loader:
    read all regular files in /etc/ws.d in alphabetical order, each file adding to
    the merged config. Only when /etc/ws.d has no files, fall back to /etc/ws.conf
    for compatibility.
    """
    conf_data = {}

    if os.path.isdir(conf_dir):
        conf_files = sorted(
            os.path.join(conf_dir, name)
            for name in os.listdir(conf_dir)
            if os.path.isfile(os.path.join(conf_dir, name))
        )
        for path in conf_files:
            with open(path, "r") as f:
                data = yaml.safe_load(f)
            if isinstance(data, dict):
                merge_yaml(conf_data, data)
        if conf_files:
            return conf_data

    # Fallback to the legacy single config file
    if os.path.isfile(conf_file):
        with open(conf_file, "r") as f:
            conf_data = yaml.safe_load(f) or {}

    return conf_data


def main():
    conf_data = load_config()

    current_uid = os.getuid()
    user = os.getenv("USER", "")

    # =========================================================================
    # ws_allocate
    # =========================================================================
    res = run_cmd(["ws_allocate", "TESTWORKSPACE", "1"])
    check("ws_allocate TESTWORKSPACE 1", res.returncode)

    # Get filesystem name via ws_list
    res = run_cmd("ws_list TESTWORKSPACE | grep 'filesystem name' | awk '{print $4}'")
    filesystem = res.stdout.strip()

    # Retrieve workspace database configuration from parsed YAML mapping
    workspaces_cfg = conf_data.get("workspaces", {}).get(filesystem, {})
    db = workspaces_cfg.get("database", "")
    dbuid = workspaces_cfg.get("dbuid")

    dbfile = os.path.join(db, f"{user}-TESTWORKSPACE") if db else ""

    # Verify DB file exists and check ownership
    db_exists = os.path.isfile(dbfile)
    check("  db found", 0 if db_exists else 1)

    if db_exists and dbuid is not None:
        dbfile_uid = os.stat(dbfile).st_uid
        check("  db owner is correct", 0 if dbfile_uid == int(dbuid) else 1)

    # Verify workspace directory and ownership
    res = run_cmd("ws_list TESTWORKSPACE | grep 'workspace directory' | awk '{print $4}'")
    directory = res.stdout.strip()

    dir_exists = os.path.isdir(directory)
    check("  workspace directory found", 0 if dir_exists else 1)

    if dir_exists:
        dir_uid = os.stat(directory).st_uid
        check("  workspace owner is correct", 0 if dir_uid == current_uid else 1)

    # Touch test file
    testfile = os.path.join(directory, "TESTFILE") if directory else ""
    try:
        if directory:
            with open(testfile, "a"):
                os.utime(testfile, None)
            check("  TESTFILE created", 0)
        else:
            check("  TESTFILE created", 1)
    except Exception:
        check("  TESTFILE created", 1)

    # =========================================================================
    # ws_release
    # =========================================================================
    res = run_cmd(["ws_release", "TESTWORKSPACE"])
    check("ws_release TESTWORKSPACE", res.returncode)
    check("  workspace removed", 0 if not os.path.isdir(directory) else 1)

    # =========================================================================
    # ws_restore (Interactive prompt execution)
    # =========================================================================
    res = run_cmd("ws_restore -l TESTWORKSPACE*")
    check("ws_restore -l TESTWORKSPACE*", res.returncode)

    res = run_cmd("ws_restore -l TESTWORKSPACE* | head -1")
    restore_id = res.stdout.strip()

    res = run_cmd(["ws_allocate", "RESTORETARGET", "1"])
    target = res.stdout.strip()

    # Pass interactive=True so stdin/stdout/stderr are linked directly to TTY
    res_restore = run_cmd(["ws_restore", restore_id, "RESTORETARGET"], interactive=True)
    check("ws_restore execution", res_restore.returncode)

    # Check if TESTFILE was restored matching pattern: ${TARGET}/*/TESTFILE
    restored_files = glob.glob(os.path.join(target, "*", "TESTFILE"))
    check("  TESTFILE restored", 0 if len(restored_files) > 0 else 1)

    # =========================================================================
    # ws_release --delete-data
    # =========================================================================
    run_cmd(["ws_release", "--delete-data", "RESTORETARGET"])
    restored_files = glob.glob(os.path.join(target, "*", "TESTFILE"))
    check("  TESTFILE removed", 0 if len(restored_files) == 0 else 1)

    # =========================================================================
    # Summary
    # =========================================================================
    print(f"\npassed: {PASS}, failed: {FAIL}")
    sys.exit(1 if FAIL > 0 else 0)


if __name__ == "__main__":
    main()
