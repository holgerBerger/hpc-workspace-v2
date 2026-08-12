# what's new in v2 vs v1

A short overview about the differences between old v1 hpc-workspace and this version,
including internals.

## new tools

- `ws_stat` shows how much diskspace a workspace uses (based on statx() with lazy size, running parallel with work-stealing queue).
  Sorting options same as `ws_list` (-N, -C, -R, -r). Thread count configurable via `-t` or `WS_THREADS` environment variable.
- `ws_editdb` allows the administrator to change DB entries in bulk. Supports pattern matching on workspace names,
  and modification modes: `--add-time`, `--add-time-expired`, `--ensure-until DATE`, `--expire-by DATE`.
  Runs in dry-run mode by default, use `--not-kidding` to execute.
- `ws_validate_config` validates configuration file syntax, required fields, and consistency (migrated from v1 and improved)
- `ws_prepare` creates filesystem directory structure according to configuration file with correct ownership and permissions

## new functionality

- `ws_list` is a lot faster due to no python startup and faster listing and reading of DB
- `ws_list -g` is a lot faster as it does only read DB entries of owners sharing a group
- `ws_list -L` shows information about available filesystems, including permissions, max duration, extensions, keeptime and a comment given by administrator
- `ws_list -T` shows a color-coded table format (red for <3 days remaining, orange for <7 days)
- `ws_list -P` shows the permissions of each workspace
- `ws_list` accepts a glob pattern argument to filter workspaces (e.g. `ws_list "experiment*"`)
- `/etc/ws.d` is primary location of config, files are read in alphabetical order and merged, if no files there,
`/etc/ws.conf` is read as fallbackt for compatibility
- `ws_release --delete-data` to wipe data while releasing a workspace (also in v1 since a while)
- `ws_restore --delete-data` to wipe data from an released or expired workspaces to reclaim disk space
- `ws_restore -l` takes an optional globbing pattern argument
- `ws_share` new commands: `sharegroup`, `unsharegroup` for group-based ACL sharing, `--readwrite` option for granting write access to groups
- in config file: `filesystems` can be used as alias for `workspaces`, to match `-F` option of tools
- in config file: `default_workspace` is an alias for `default`
- in config file: `maxduration` is an alias for `duration`
- in config file: ACL syntax has `-` and `+`
- in config file: extended ACL syntax `[+|-]id[:[permission{,permission}]]` with permission in `list,use,create,extend,release,restore`
  allows to restrict single users or groups e.g. to use old workspaces in a filesystem but not extend them
- in config file: `releasekeeptime` is the time in days a user released workspace is kept, default to `keeptime` 
- same executable can be used with setuid or capabilities (if capability support is detected at build time)
- `--version` switch can be used to see if capability or setuid is available and used
- most tools have `--config` option, which allows using workspace tools without privileges in users own directories and with own config file. This is usefull for testing.

## changed behaviour

- field `adminmail` as a list of email addresses is required in config (gives warning when not present)
- `ws_release` does not keep the workspace for `keeptime` but for a `releasekeeptime`, only expired workspace are kept `keeptime`
- `ws_release` bails out if workspace name is not unique
- `ws_allocate -x` bails out if workspace name is not unique
- `ws_allocate -g` can take an groupname as well, -G can have no groupname
- all group workspaces have group sticky bit
- lua callout for path building is no longer supported, allocation options should fully replace its functionality
- user needs access to default workspace (was giving a warning in v1 for some years already)
- each tool does some checks on config validity and can bail out if config is bad, even for fields it does not use
- new human check in `ws_restore`, avoid dependency on `terminfo` or `ncurses`

## what's new under the hood

- a lot more tests (unit tests with Catch2, integration tests with bats)
- CI pipeline
- no python dependency, all tools are C++ or shell scripts
- some tools use threading for parallel processing
- abstraction of the DB, allowing easier tool development and will allow new functionality in DB in a coming version, planned is more privacy through better isolation of users/groups
- compile-time and runtime detection of capability/setuid/usermode privilege handling
- dependencies to `Catch2`, `curl`, `{fmt}`, `GSL`, `yaml-cpp`, `rapidyaml`, `spdlog`, `bshoshany/thread-pool`
- `curl` and `boost` have to be installed from distribution, all others are compiled as part of building hpc-workspace-v2
- Docker and Vagrant (Rocky Linux 8/9) based testing infrastructure
- RPM packaging: a spec file `hpc-workspace-v2.spec` in the top level directory allows building an RPM
