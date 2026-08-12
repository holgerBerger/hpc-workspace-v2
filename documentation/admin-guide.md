# Workspace Administrators Guide

You can read this guide at
https://github.com/holgerBerger/hpc-workspace-v2/blob/master/admin-guide.md with
markup.

## Motivation

The motivation for these workspace tools was the need to loadbalance a large
number of users over a medium size number of scratch/working filesystems in an
HPC environment by the operations team without any manual interaction.

The basic idea is

- a workspace is a directory created on behalf of the user on demand
- the lifetime is limited, the directory will be deleted automatically at some
  point in time
- the location is determined by the administrator and can contain a random
  component

This approach allows the administrator a flexible allocation of resources to
users. It offers a level of redirection to hide details from the user, it
offers a stable interface to the user, it allows e.g. migration of users
between different filesystems - although only over medium to large time scales,
and it offers a way to get a little grip on the lifetime of data. If no one
takes care of the data anymore, it will get deleted at some point.

Administrators can assign different filesystems to users based on user and
group and can optionally loadbalance over several filesystems.

Typically, a workspace will be created
- on a fast filesystem for temporary data, probably no backups, not intended
  for long time storage, probably faster than the typical home
  filesystem, think of a parallel filesystem like Lustre or
  StorageScale or BeeGFS.
- for the duration of a job or a job campaign or project.

Typically, a workspace will be deleted

- because the job or campaign ended, and the user releases the directory, or
- because the maximum lifetime of the workspace is reached

A friendly user or a user short on quota probably wants to remove the data
before releasing the workspace to regain quota.

The workspace tool set offers the possibility to keep expired data for some
time in a restorable state, and users can restore the data without
administrator intervention using the ```ws_restore``` command.

Most operations are logged to syslog, so the administrator can audit what was done by the user.
The deletion process also does extensive logging.

## Basic components

The tool set's main components are user-visible commands (`ws_allocate`,
`ws_release`, `ws_list`, `ws_restore`, `ws_extend`, `ws_find`, `ws_register`,
`ws_send_ical`, `ws_stat`, `ws_share`) and the administrator's tools
(`ws_expirer`, `ws_editdb`, `ws_validate_config`, `ws_prepare`),
and the configuration file.

All configuration is in ```/etc/ws.d``` (files read in alphabetical order and merged, new in v2)
or in ```/etc/ws.conf``` as a fallback for compatibility.

Using ```/etc/ws.d``` allows splitting the configuration into multiple files, e.g. one file per
filesystem or one base config and separate filesystem definitions. Files are read in alphabetical
order, so you can use naming like ```00-global.yaml```, ```10-scratch.yaml```, ```20-lustre.yaml```
to control the merge order.

## Installation

There are two ways to install the tool set: as an RPM package on RPM based
distributions, or by building and installing from source. Both use the same
dependencies and the same privilege model, those are described first.

### Dependencies

The workspace tools use CMake for configuration and building, make sure it is
installed, you will also need a C++ compiler for C++17, compiling with GCC and clang is
tested (on Ubuntu and Redhat compatible distributions).

Furthermore, it uses the boost components ```system, program_options```, and
it also needs ```libcurl``` from your distribution.
You can use ```cd external; ./getexternals.sh; cd ..``` to get and compile some additional
dependencies into the source directory. Using distribution based libraries for those is not supported at the moment.

The complete list of dependencies is:
- C++ compiler at level C++17 (g++ and clang++ tested)
- boost_system
- boost_program_options
- libcap2 (if using capabilities)
- libcurl

### Capabilities and setuid

This version has compile-time detection of whether the capability version can be built
and checks at runtime if capabilities are set or the setuid bit is present.

The following tools need privileges to work: `ws_allocate`, `ws_release` and `ws_restore`.
Be aware that root_squash filesystems (tested with lustre) might require
the capability version, and some extra settings [^1] to allow the required capabilities.

With V2, the setuid version and the capability version are both under regression testing.

You can check ```ws_allocate -V``` to see if capability mode was compiled in.


### Installation with rpm

On RPM based distributions (RHEL 8/9/10 and compatible) the tool set can be built and
installed as an RPM package. The spec file ```hpc-workspace-v2.spec``` is part of the
repository, in the top level directory.

#### 1. Setup

Install the build tooling and the dependencies:

```
sudo dnf install -y rpm-build rpmdevtools gcc-c++ cmake make boost-devel libcurl-devel libcap-devel git
```

Create the rpmbuild tree and copy the spec file into it. ```rpmdev-setuptree``` creates
the tree in ```~/rpmbuild```, unless ```%_topdir``` is set to something else in
```~/.rpmmacros```, in that case adapt the paths in the following steps:

```
rpmdev-setuptree
cp <path-to-repo>/hpc-workspace-v2.spec ~/rpmbuild/SPECS/
```

Check the following before building, and adapt the spec file if needed:
- the capability build is the default (```-DWS_USE_CAPABILITIES=ON```), for the setuid
  version follow the comments in the ```%files``` section of the spec file
- the CMake build type is ```RelWithDebInfo```
- the package installs all user tools to ```%{_bindir}```, the administrator tools
  (```ws_expirer```, ```ws_editdb```, ```ws_prepare```) to ```%{_sbindir}```, and the man pages

#### 2. Stage the source tarball

The tarball's top level directory has to match ```hpc-workspace-v2-<version>```:

```
git -C <path-to-repo> archive --format=tar.gz --prefix=hpc-workspace-v2-2.0.0/ \
    -o ~/rpmbuild/SOURCES/hpc-workspace-v2-2.0.0.tar.gz HEAD
```

#### 3. Build

```
rpmbuild -bb ~/rpmbuild/SPECS/hpc-workspace-v2.spec
```

#### 4. Install

```
sudo dnf install ~/rpmbuild/RPMS/$(uname -m)/hpc-workspace-v2-2.0.0-1.*.rpm
```

### Installation from source

Run ```cmake --preset release``` and ```cmake --build --preset release -j``` to configure and compile the tool set.

The build system can automatically set capabilities or setuid permissions during install:
- By default, if libcap is found, it uses capabilities (set with setcap)
- Without libcap or with ```-DWS_USE_CAPABILITIES=OFF```, it uses setuid (chmod u+s)
- To customize the UID/GID for setuid, use ```-DWS_SETUID_UID=<uid> -DWS_SETUID_GID=<gid>```
- To disable automatic privilege setting, use ```-DWS_INSTALL_SET_PRIVILEGES=OFF```

Execute ```cmake --install build/release --prefix <TARGET>``` to install executables
and man pages and set file properties correctly.

#### Manual installation

If you can not use the automatic installation, e.g. as your setup is more complex do those steps by hand:

Copy the executables from ```bin``` to e.g. ```/usr/local/bin``` and the
manpages from ```man``` to e.g. ```/usr/local/man/man1```.

make `ws_allocate`, `ws_release`, `ws_restore` setuid root, *or* execute
```
setcap "CAP_DAC_OVERRIDE=p CAP_CHOWN=p CAP_FOWNER=p" ws_allocate
setcap "CAP_DAC_OVERRIDE=p CAP_CHOWN=p CAP_FOWNER=p" ws_release
setcap "CAP_DAC_OVERRIDE=p CAP_DAC_READ_SEARCH=p" ws_restore
```
*if* capability library was available at compile time.

### Setting up the expirer

Finally, a cron job has to be set up that calls the `ws_expirer` tool at
regular intervals, only then will old workspaces be cleaned up. The
`ws_expirer` setup is detailed below.

[^1]: for lustre you might need for newer versions ```lctl set_param -P mdt.<fsname>-*.enable_cap_mask=+cap_dac_read_search,cap_chown,cap_dac_override,cap_fowner```
to exclude those capabilities from being dropped in root_squash mode as well.

## Further preparation (```ws_validate_config``` and ```ws_prepare```)

You will need a uid and gid which will serve as owner of the directories above
the workspaces and foremost for the DB entry directory.

You can reuse an existing user and group, but be aware that anybody able to use
that user can manipulate other peoples' DB entries, and that the setuid tools
spend most of their time with the privileges of that user. Therefore, it makes
sense to have a dedicated user and group ID, but it is not a hard requirement,
you could also reuse a user and group of another daemon or tool.

It is good practice to create the configuration and validate it with
```ws_validate_config```. This tool checks for required fields
and configuration consistency.

It is also good practice to use ```ws_prepare``` to create the
filesystem structure according to the config file.

## Getting started

A very simple example `ws.conf` file:

```yaml
admins: [root]			# users listed here can see all workspaces with ws_list
adminmail: [root@localhost]      #! add something meaningful here, it is used to alert of bad conditions
clustername: My Green Cluster	# some name for the cluster
smtphost: mail.mydomain.com     # my smtp server for sending mails
dbuid: 85			#! a user id, this is the owner of some directories
dbgid: 85			#! a group id, this is the group of some directories
default: ws1			#! the default workspace location to use for everybody
duration: 10                    # maximum duration in days, default for all workspaces
maxextensions: 1                # maximum number of times a user can ask for an extension
filesystems:              #! or 'workspaces' for compatibility
  ws1:				#! name of the workspace location
    comment: "for all users"
    database: /tmp/ws/ws1-db	#! default DB directory
    deleted: .removed		#! name of the subdirectory used for expired workspaces
    duration: 30		# max lifetime of a workspace in days
    keeptime: 7			# days to keep deleted data after expiration
    maxextensions: 3		# maximum number of times a user can ask for an extension
    spaces: [/tmp/ws/ws1]	#! paths where workspaces are created, this is a list and path is picked randomly or based on uid or guid
```

**Note:** the lines with ```#!``` as comment are mandatory for a valid config file.

In this example, any workspace would be created in a directory in
```/tmp/ws/ws1``` whenever a user calls ```ws_allocate```, and he would be able
to specify a lifetime of 30 days, not longer, and he would be able to extend
the workspace 3 times before it expires.

When the `ws_allocate` command is called, for example with ```ws_allocate BLA
1```, it will print the path to the newly created workspace to `stdout` and
some additional info to `stderr`. This allows using `ws_allocate` in scripts
like the following example:
```bash
SCR=$(ws_allocate BLA 1)
cd $SCR
```

and ```ws_list -t``` should then show something like

```yaml
id: BLA
     workspace directory  : /tmp/ws/ws1/user-BLA
     remaining time       : 0 days 23 hours
     available extensions : 3
```

As you can see, the username is prefixed to the workspace ID in the path of the
workspace. Users should not rely on that, this could change over time.

```ls -ld /tmp/ws/ws1/user-BLA``` will reveal that the user who created the
workspace is the owner of the directory and has read and write permissions,
otherwise it should be private.

**Note**: Make sure the ```database``` directory is owned by ```dbuid``` and
```dbgid``` !

## Full breakdown of all options

### Global options

#### `clustername`

Name of the cluster, shows up in some outputs and in email warning before
expiration.

#### `smtphost`

FQDN of SMTP server (no authentication supported), this is used to send
reminder mails for
expiring workspaces and to send calendar entries.


#### `mail_from`

Used as sender in any mail, should be of the form ```user@domain```.

#### `default` (alias: `default_workspace`)

Important mandatory option, this determines which workspace location to use if not
otherwise specified.

If there is more than one workspace location (i.e. more than one entry in
`workspaces`), then the location specified here will be used for all workspaces
by all users. A user may still manually choose the location with ```-F```.

#### `duration` (alias: `maxduration`)

Maximum lifetime in days of a workspace, can be overwritten in each filesystem
location specific section.

#### `durationdefault`

Lifetime in days attached to a workspace if user does not specify it.
Defaults to 1 day.

#### `reminderdefault` 

if this is set, users always get a reminder email n days before expiration.
If no email is set, the mail is send to local username.

#### `maxextensions`

Maximum number of times a user can extend a workspace, can be overwritten in
each workspace location specific section.

#### `dbuid`

UID of the database directory, and the UID that will be used by all setuid
tools (as long as UID 0 is not required). Can be a shared UID, but be aware
the user using that UID can mess with the DB.
It is strongly suggested to use a dedicated UID or an UID of another daemon.

#### `dbgid`

GID of the database directory, and the GID that will be used by all setuid
tools (as long as GID 0 is not required). Can be a shared GID, but be aware
users assigned to that GID can mess with the DB. It is strongly suggested
to use a dedicated GID or a GID of another daemon.

#### `debugusers`

A list of users who can use the `--debug` and `--trace` flags.
If a user sees unexpected behaviour, add the username to this list
and run again and check journal for debug output.

#### `admins`

A list of users who can see any workspace when calling ```ws_list```, not
just their own.

#### `adminmail`

A list of email addresses to inform when a bad condition is discovered by ws_expirer
which needs intervention.

#### `expirerlogpath`

A path including filename where ```ws_expirer``` will place a logfile per day.
The logfile contains additional timestamps compared to normal output.

#### `deldirtimeout` (alias `deldir_timeout` for v1 compatibility)

A timeout value in seconds that a single workspace deletion is allowed to take.
If this time is exceeded, the workspace might not be fully deleted, but
deletion will be resumed in the next instance of the ```ws_expirer```.
Default is 300s.
**remark** This timeout is not hard and might be missed in current implementation.

#### `maxuserworkspaces`

The maximum number of workspaces a user can create in total across all filesystem locations.
This is to prevent e.g. endless creation of workspaces by malformed loops.
If this is 0, it is ignored.

### Filesystem specific options

In the config entry `filesystems` (alias `workspaces` for v1 compatibility), multiple workspace location entries may be
specified, each with its own set of options. The following options may be
specified on a per-workspace-location basis:

#### `keeptime`

Time in days to keep data after it was expired. This is an option for the
cleaner. The cleaner will move the expired workspace to a hidden location
(specified by the `deleted` entry below), but does not delete it immediately.
Users or administrators can still recover the data. After `keeptime` days,
it will be removed and cannot be recovered anymore.

If this is not set for a workspace, it defaults to value of 10 days.

**Note:** `keeptime` does not apply to user released workspaces, only to expired workspaces.

#### `releasekeeptime`

Time in days to keep data after it was released by user using `ws_release`.
When this is not set for a workspace, it defaults to the value of `keeptime`.

**Note:** compare to `keeptime` above, besides targeting released workspaces, it behaves otherwise the same.

#### `spaces`

A list of directories that make up the workspace location. The directory for
new workspaces will be picked randomly from the directories in this list by default,
see ```spaceselection``` for ways to customize this.

This can be used to distribute load and storage space over several filesystems
or fileservers or metadata domains like DNE in Lustre.

#### `spaceselection`

can be `random` which is default, or `uid` or `gid` to select space based on
modulo operation with uid or gid to select a stable space for a user (useful to avoid cross filesystem
moves), or `mostspace` to choose the filesystem with most available disk space.

#### `deleted`

The name of the subdirectory, both inside the workspace location and inside the
DB directory, where the expired data is kept. This is always inside the space
to prevent copies of the data, and to allow rename operation to succeed for
most filesystems in most cases by avoiding cross filesystem or namespace renames.

#### `database`

The directory where the DB is stored. The DB is currently simply a directory having one
YAML file per workspace.

This directory should be owned by `dbuid` and `dbgid`, see the corresponding
entries in the global configuration.

If your filesystem is slow for metadata, it might make sense to put the DB on
e.g. a NFS filesystem, but the DB is not accessed without any reason and should
not be performance-relevant, only ```ws_list``` might feel faster if the
filesystem with the DB is fast in terms of iops and metadata.
For lustre, a DOM directory might make sense.

#### `duration` (alias: `maxduration`)

Maximum allowed lifetime of a workspace in days for this filesystem location. User may not specify a longer
duration for his workspaces than this value. Overrides the global `duration` setting.

#### `groupdefault`

Lists which groups use this location by default. Any user that is a member of
one of the groups in this list will have their workspaces allocated in this
workspace location. This overrides the `default` in the global config. A user
may still manually pick a different workspace location with the ```ws_allocate
-F``` option.

**Caution:** if a group is listed in the `groupdefault` list of several
workspace locations, this results in undefined behavior. This condition is not
tested for, the administrator has to ensure that this does not happen.

#### `userdefault`

Lists users which use this location by default. Any user in this list will have
their workspaces allocated in this workspace location. This overrides the
`default` in the global config. A user may still manually pick a different
workspace location with the ```ws_allocate -F``` option.

**Caution:** if a user is listed in the `userdefault` list of several workspace
locations, this results in undefined behavior. This condition is not tested
for, the administrator has to ensure that this does not happen.

#### `user_acl`

List of users who are allowed to choose this workspace location. If this list
and `group_acl` are both empty, all users may choose this location.

As soon as the list exists and is not empty, this list joined with `group_acl`
is matched against the user and his group. If the user is not in either of the
two lists, he may not create a workspace in this location.

With v2 there is a new extended ACL syntax introduced:
An ACL list entry now has the format `[+|-]id[:[permission{,permission}]]` with permission being one of  `list,use,create,extend,release,restore`.
Be careful with the permission list, the comma separator is the YAML list separator as well, use quoting to overcome that problem.

Example: `user_acl: ["usera:list,release","userb:list,release"]`,
To make a workspace at all usable for a user, `list` is always required,
Extended ACL syntax is only needed in very special situations, if single users should be prevented from carrying out some operations.

**Caution:** in v1, the global `default` workspace enabled access to the named
workspace for all users, this is no longer true in v2. Users have to have access
to the named workspace.

**Hint**: To enable access control, at least one of `user_acl` or `group_acl`
has to be existing and non-empty! An invalid entry can be used to enable access
control, like a non-existing user or group. An empty list does not enable
access control, the workspace can still be accessed with an empty list by all
users!

#### `group_acl`

List of groups who are allowed to choose this workspace location.  If this list
and `user_acl` are both empty, all users may choose this location.

See `user_acl` for further logic.

**Hint**: to enable access control, at least one of `user_acl` or `group_acl`
has to be existing and non-empty! An invalid entry can be used to enable access
control, like a non-existing user or group. An empty list does not enable
access control, the workspace can still be accessed with an empty list by all
users!


#### `maxextensions`

This specifies how often a user can extend a workspace, either with
```ws_extend``` or ```ws_allocate -x```. An extension is consumed if the new
duration ends later than the current duration (in other words, you can shorten
the lifetime even if you have no extensions left) and if the user is not root.
Root can always extend any workspace using ```-u``` option.

#### `allocatable`

Default is ```yes```. If set to ```no```, the location is non-allocatable,
meaning no new workspaces can be created in this location.

This option, together with the `extendable` and `restorable` options below, is
intended to facilitate migration and maintenance, i.e. to phase out a
workspace, or when moving the default of users, e.g. to another filesystem.

#### `extendable`

Analog to `allocatable` option above. If set to `no`, existing workspaces in
this location cannot be extended anymore.

#### `restorable`

Analog to `allocatable` option above. If set to `no`, workspaces cannot be
restored to this location anymore.

## Internals

V2 is the second rewrite of the tools, first version was in python with
some horrible setuid hacks, second version was partially in C++.
C++ aims to be fully C++ or shell scripts. V2 offers an internal abstraction of the DB and configuration, which will allow having
a new DB format in the future, and enables easier creation of tools with consistent behaviour.

V2 first DB implementation is compatible with V1 DB.

A DB file is currently still a YAML file, this can change in the future.

There are three tools that need privileges, these are ```ws_allocate```,
```ws_release``` and ```ws_restore```.

All three have to change owners and permissions of files.

All other tools are either for root only (in ```sbin```) or do not need
privileges (```ws_list```, ```ws_extend```, ```ws_find```, ```ws_register```,
```ws_send_ical```).

The basic setup consists of at least two directory trees, one for the DB and
one for the data. These trees have to be separate and neither may be a
subdirectory of the other. They may reside on different filesystems, but do not
have to. If they do, be careful to mount them at the same time.
The database directory has to contain a file .ws_db_magic with the name
of the workspace in it, this is used by the ws_expirer to verify that the DB is present
and valid, to avoid e.g. problems with not mounted filesystems.

A typical setup could look like this:

```
/tmp/ws -+- ws1-db -+              (owned by dbuid:dbgid, permissions drwxr-xr-x)
		 |          +- .ws_db_magic (containing name of ws, ws1 in the example)
         |          +- .removed    (owned by dbuid:dbgid, permissions drwx------)
         |
         +- ws1-----+              (owned by anybody, permissions drwxr-xr-x)
                    +- .removed    (owned by anybody, permissions drwx------)
```

This is the structure that would result from the example config file shown
above.

In this case, ```ws1-db``` is the database location, corresponding to the
```database``` entry in the config file, ```ws1``` corresponds to the single
entry in the ```spaces``` list, and the `.removed` directories are the
locations of expired entries for both the spaces and the DB, corresponding to
the ```deleted: .removed``` config file entry.

Whenever a workspace is created, an empty directory is created in ```ws1```,
this directory is owned by and writable for the user who created the workspace.
Additionally, a file with the DB entry will be created in ```ws1-db```, owned by
```dbuid:dbgid``` but readable by all users. Both the directory and the file
have the naming convention of ```username-workspacename```, so several users
can have a workspace with the same name.

If a workspace is expired or released, both its workspace directory and the DB
entry file are moved into the corresponding ```deleted``` directories (called
```.removed``` in this example) and get a timestamp with the time of deletion
appended to the name. This ensures that there can be several generations of a
workspace with the same name from the same user that exist in parallel in the
restorable location.

**Caution:** since the moved data is still owned by the user, only in a
non-accessible location, it is still counted towards the user's quota. Users
who want to free the space have to restore the data with ```ws_restore```,
delete it, and release it again, or they can use ```ws_restore --delete-data```
to wipe it.

**Caution:** make sure that the DB and the workspace directory are available
when the expirer is running, a missing DB (due e.g. a missing mount if in a different
filesystem) can be fatal. This is supported and checked with the magic file.
It is advisable to have both DB and data in same filesystem.
For performance reasons, it can be advisable to have for Lustre the DB in a DOM directory.

It is the task of the cleaner, a part of the `ws_expirer` program, to iterate
through the spaces to find if there is anything looking like a workspace not
having a valid DB entry, and iterate through the deleted workspaces to check
how old they are, and whether they should still be kept or be deleted.
Furthermore, it checks the DB entries if any of them are expired, and moves the
entry and the directory to the deleted directory if needed. The cleaner is only
enabled if the `--cleaner` (or `-c`) option is specified when calling
`ws_expirer`.

**Warning:** anything in a spaces directory can be deleted by the expirer!

## Setting up the ```ws_expirer```

The `ws_expirer` is the tool which takes care of expired Workspaces. To set
it up, create a daily cron job that runs the `ws_expirer` script:

```
10 1 * * * /usr/sbin/ws_expirer -c
```

Note the required `-c` option. This option enables the cleaner. If it were left
out, `ws_expirer` would be running in "dry-run" mode, which is a testing
feature, and would not perform any file operations.

**Note:** It is strongly recommended to test a configuration in dry-run mode first
and study the output of a manual run before setting up a cronjob!

v2 introduced a new logging scheme, you can use the ```expirerlogpath``` option in the config file to
write a daily rotated logfile.

The `ws_expirer` operates in two phases:
1. **Stray directory cleanup**: Scans workspace spaces for directories without valid DB entries. Directories matching the `username-workspacename` pattern are moved to the deleted directory. Directories not matching the expected pattern are logged and ignored, requiring manual intervention.
2. **Database-based expiration**: Processes all DB entries — active workspaces past their expiration are moved to the deleted directory, and deleted workspaces past their keeptime are permanently removed.

The `ws_expirer` also sends **reminder emails** to users before their workspaces expire (if `smtphost` and `mail_from` are configured and the workspace has a mail address in its DB entry).
It sends **error notifications** to administrators (via `adminmail`) when DB errors or critical conditions are encountered.

## Editing the Database (```ws_editdb```)

`ws_editdb` is an admin-only tool for bulk-modifying workspace database entries by
pattern matching on workspace names and/or usernames. It is useful for tasks like
extending workspaces before a cluster maintenance window or enforcing a hard expiry
date across a group of workspaces.

**By default the tool runs in dry-run mode** and only shows what would be done. Pass
`--not-kidding` to actually apply the changes.

Only one modification option can be active at a time:

| Option | Effect |
|--------|--------|
| `--add-time <DAYS>` | Add days to the expiration time of matching workspaces |
| `--add-time-expired <DAYS>` | Add days to the expired time (workspaces in the recovery area) |
| `--ensure-until <YYYY-MM-DD>` | Extend workspaces that would expire before the given date |
| `--expire-by <YYYY-MM-DD>` | Shorten workspaces that would expire after the given date |

Use `-e` to select expired workspaces (those in the recovery area) instead of active ones.
Use `-u <USER>` or a glob pattern to narrow the selection.

Examples:

```
# Preview: extend all workspaces matching "project*" by 7 days
ws_editdb --add-time 7 "project*"

# Apply: ensure all workspaces last until at least 2026-06-01
ws_editdb --ensure-until 2026-06-01 --not-kidding

# Apply: cap all workspaces to expire no later than 2026-05-01
ws_editdb --expire-by 2026-05-01 --not-kidding

# Apply: give expired workspaces of user alice 30 more days in the recovery area
ws_editdb -e -u alice --add-time-expired 30 --not-kidding
```

## Contributing

Contributions are highly welcome. Please refer to the
[issue tracker](https://github.com/holgerBerger/hpc-workspace-v2/issues) of this project and
[discussions](https://github.com/holgerBerger/hpc-workspace-v2/discussions)
