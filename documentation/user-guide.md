# Workspace user guide

For the latest version (which might not fit your installation) see
    https://github.com/holgerBerger/hpc-workspace-v2/blob/master/user-guide.md


## Tool overview

| Tool | Purpose |
|------|---------|
| [`ws_allocate`](#creation-of-a-workspace-ws_allocate) | Create, extend, or modify a workspace |
| [`ws_list`](#listing-workspaces-ws_list) | List your workspaces and available filesystems |
| [`ws_stat`](#workspace-disk-usage-statistics-ws_stat) | Show disk usage statistics for workspaces |
| [`ws_find`](#finding-a-workspace-ws_find) | Print the path of a workspace by ID |
|[ `ws_register`](#registering-workspaces-as-symlinks-ws_register) | Maintain a directory of symlinks to your workspaces |
| [`ws_release`](#releasing-and-restoring-workspaces-ws_release-and-ws_restore) | Release (expire) a workspace |
| [`ws_restore`](#releasing-and-restoring-workspaces-ws_release-and-ws_restore) | Restore a previously released workspace |
| [`ws_extend`](#extending-workspaces-ws_extend-or-ws_allocate--x) | Extend the lifetime of a workspace |
| [`ws_share`](#cooperative-usage-group-workspaces-and-sharing-with-other-users) | Share a workspace with other users via ACLs |
| [`ws_send_ical`](#creation-of-a-workspace-ws_allocate) | Send a calendar reminder for workspace expiry |

For a full list of options, use -h or see the respective man page.


## Motivation

High performing parallel and reliable storage is a very expensive resource, and optimizing its usage is in everybody's interest.

*workspaces* are a concept allowing the operations team of an HPC resource to offload some
tasks to the user, and allow the user to keep easier track of job directories.
It also allows the operations team to manage and load balance several filesystems,
and hiding this fact from the users, as well as e.g. migrating data from one filesystem to another.

A *workspace* is foremost a directory created on behalf of the user with some properties
- it has an *ID* selected by the user
- it has a lifetime selected by the user, but limited by the operations team
- it has permissions the user can influence or change
- different qualities of storage can be offered with one interface

The *workspace* is the place where big data remains during a job or during a job campaign.
It is probably not the place to store source files, and it is not the place to archive data at.

Any *workspace* will be deleted at some point in time, so better keep track of them.
But this property makes sure the nasty user from the other side of the floor does not use
all the space for ages.

## Creation of a workspace (```ws_allocate```)

A workspace can be created with *ws_allocate*.

Example:

```
SCRDIR=$(ws_allocate MySpace 10)
```

will create a *workspace* with the ID *MySpace* existing for 10 days.

The command will return the path to that new directory into *SCRDIR*,
and will print some more information on stderr.

The maximum lifetime for a workspace may be limited by the operations team. If
you specify a longer lifetime, it will be capped to the maximum, and you will see
a message that it was changed. If you do not specify a lifetime, a default
lifetime will be used (typically 1 day).

There might be several filesystems prepared by the operations team where a workspace
can be created, you can find out those filesystems with ```ws_list -l``` or ```ws_list -L```.
Filesystems are listed sorted by priority. If you do not specify a filesystem, the first
one in the list (highest priority) will be chosen. You can otherwise
choose the filesystem using ```ws_allocate -F <LOCATION> <ID> <DURATION>```.

**Important:** If a workspace with the given ID already exists, it will be reused and
the same path is returned — the duration is not reset. It is therefore safe and encouraged
to use such a line in batch jobs which are part of a series of jobs working
on the same data, no matter if the job was running before or not.

You can use ```ws_find <ID>``` instead as well, if you feel more comfortable.

To get a reminder email before the workspace expires, you can set a reminder alarm
using ```ws_allocate -m <MAILADDRESS> -r <DAYS> <ID> <DURATION>```.

You can store default values for reminder and email in ~/.ws_user.conf.
Defaults in file can be overruled with command line options.

You can change reminder and email address of an existing workspace using ```ws_allocate -r <DAYS> -m <MAILADDRESS> -x <ID> 0```.

You can also generate a calendar entry via email with ```ws_send_ical```, see manpage for more details.

## Listing workspaces (```ws_list```)

```ws_list``` will list all your owned workspaces. This has many options for verbosity
(*-s* only names, *-t* less than default, *-v* more than default) and sorting
(*-N* for name, *-C* date of creation, *-R* remaining time, *-r* reversed)

*-l* shows the available locations, and *-F* limits the locations of the listing.

A short overview of the workspaces in table format listed for remaining time can be displayed
with ```ws_list -RTt```.

## Workspace disk usage statistics (```ws_stat```)

```ws_stat``` shows disk usage statistics for your workspaces: number of files, symlinks,
directories, and total size in bytes. Without arguments it scans all your workspaces;
a glob pattern can be given to limit the output to matching workspace IDs.

By default only your own workspaces are shown. Add *-g* to also include group workspaces
that are visible to your current group.

Sorting works the same as in ```ws_list``` (*-N* name, *-C* creation date, *-R* remaining time,
*-r* reversed). Add *-v* to also print the scan time and throughput per workspace.

The scan uses parallel directory traversal. The number of threads defaults to the number of
CPU cores and can be overridden with *-t* or the ```WS_THREADS``` environment variable.

## Finding a workspace (```ws_find```)

```ws_find <ID>``` prints the path of a workspace by name, useful as an alternative to
```ws_allocate``` when you want to locate an existing workspace without (accidentally) creating one.

By default it searches in all filesystems. Use ```-F <FILESYSTEM>``` to limit the search
to a specific one.

To search for group workspaces, add ```-g```. To search workspaces of another user, use ```-u <USERNAME>```.

## Registering workspaces as symlinks (```ws_register```)

```ws_register <DIRECTORY>``` creates symbolic links to all your workspaces inside the given
directory, organized into subdirectories per filesystem. Links pointing to workspaces that no
longer exist are removed automatically, keeping the directory in sync.

This is useful if you want to browse or access your workspaces conveniently through a fixed
location in your home directory, e.g.:

```
ws_register ~/ws_links
```

Use ```-F <FILESYSTEM>``` to restrict the links to a specific filesystem.

## Releasing and restoring workspaces (```ws_release``` and ```ws_restore```)

```ws_release <ID>``` releases a workspace.
Releasing means that the ID can be reused and the directory is not accessible any more,
but it does not delete the data immediately.
The data is probably kept for a while if there is enough space and can be recovered using
the ```ws_restore <ID> <TARGET>``` command as long as it is not finally deleted.

Once a workspace expires or is released, its ID changes, it gets a timestamp
appended and is no longer identical to the name shown by ```ws_list```. Use
```ws_restore -l``` or ```ws_list -e``` to list the available names for restoration. Additionally TARGET must be an
already-existing workspace, ```ws_restore``` restores the data into it, it does not
create a new workspace.

**Note:** ws_release makes the workspace a candidate for deletion: if you released it by accident,
restore it quickly. An expired workspace that you forgot to extend might linger for a while, but a released workspace
gets deleted a lot earlier, e.g. during the next night.
The real deletion will probably take place nightly.

If you are sure you do not need the data anymore, there is an option ``--delete-data`` for ```ws_release``` and ```ws_restore``` to
wipe the data while releasing it — **use with care**.

**Please note:** Data in a released workspace can still account for the quota usage!
In case the data is limiting you, delete the data before releasing the workspace, or use the ``--delete-data`` option with care.
In addition, ```ws_restore --delete-data <ID>``` permanently deletes the data of an already-released
workspace without restoring it first — useful when you need to free quota. **Use with care**, the data cannot be recovered.

## Extending workspaces (```ws_extend``` or ```ws_allocate -x```)

As each workspace has an expiration date, its lifespan is limited.
The operations team can allow you a certain number of extensions of a workspace,
you can see the amount of available extensions with ```ws_list```.

You can extend a workspace using ```ws_allocate -x <ID> <DAYS>``` or ```ws_extend <ID> <DAYS>```,
each call will consume an extension, unless the new expiration date is shorter
than the previous one. You can also shorten the lifetime if no extensions
are available anymore.

You can extend group members workspaces if they had been created using ```ws_allocate -G <GROUPNAME>```
(to create a writable group workspace) using ```ws_allocate -x -u <USERNAME> <ID> <DAYS>```.

## Changing a workspace

Some attributes can be changed after creation: the comment, the reminder time, and the mail address for the reminder.

This is achieved with ```ws_allocate -x```. If no duration is given this does not consume an extension.

example:
```
$ ws_allocate -x -c "new shiny comment" myworkspace
```

## Cooperative usage (group workspaces and sharing with other users)

When a workspace is created with ```-g <GROUPNAME>``` it gets a group workspace that is visible to others with ```ws_list -g``` (if in same group),
and is group readable and gets group sticky bit.

When it is created with ```-G <GROUPNAME>``` the workspace gets writable as well, and gets group sticky bit. The group can be specified in
the ~/.ws_user.conf file as well as ```groupname```.

A writable workspace can also be listed by group members with ```ws_list -g``` and it can in addition be extended
using ```ws_allocate -x -u <USERNAME> <ID> <DAYS>```.

**Please note:** the option parser needs either another option behind ```-g``` and ```-G``` if no groupname is given, or a ```--``` to separate options from position arguments.

Syntax examples:
```
$ ws_allocate -G -- myworkspace
```

```
$ ws_allocate -G --comment "no group" myworkspace
```

```
$ ws_allocate -G rollingstone myworkspace
```

With ```ws_share``` you can share workspaces with users outside your group, using ACLs (if supported by underlaying filesystem)

```ws_share share <ID> <USERNAME>``` gives read access to the specified user, ```ws_share unshare <ID> <USERNAME>``` removes the access.

Those operations are applied to all files and directories in the workspace.

## User defaults and options with ~/.ws_user.conf file

Some defaults can be set in ~/.ws_user.conf, so you do not have to give them on command line all the time.
The file is in YAML syntax, and can have the following keys: ```mail```, ```duration```, ```reminder``` and ```groupname```
and comments.

Example:
```
# example file
mail: reach@me.here
duration: 10
reminder: 3
groupname: abba
```

Furthermore, the option ```expirationmail: false``` can be used to disable expiration mails, if you receive mails from the system each time 
a workspace expires.
