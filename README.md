# hpc-workspace-v2

This is the construction site of next major version of hpc-workspace++ tool.

**DO NOT YET USE IN PRODUCTION - unless you know what you do and want to test**

This is the next major version of the hpc-workspace tool set for managing temporary workspace directories
on HPC systems.

This version has reached a usable state and offers several new tools
and features not present in v1. The database format is backwards compatible with v1,
config files can be reused with light adjustments.

Please use the [discussion tab](https://github.com/holgerBerger/hpc-workspace-v2/discussions) if you would like to share input.

Please report issues at [bugtracker](https://github.com/holgerBerger/hpc-workspace-v2/issues)

## What is this about

A **workspace** is a directory, with an associated expiration date, created on behalf of a user, to prevent disks from uncontrolled filling.
The project provides user and admin tools to manage those directories.

Example for usage:

In a batch job, you write

```
SCR=$(ws_allocate MyData 10)
cd $SCR
```

This will set you into a temporary directory that will last 10 days.

You can check which **workspaces** you have using

```
$ ws_list
id: MyData
     workspace directory  : /tmp/username-MyData
     remaining time       : 9 days 23 hours
     creation time        : Wed Mar 13 23:51:57 2013
     expiration date      : Sat Mar 23 23:51:57 2013
     available extensions : 15
```

and you remove the directory if you do not need it anymore with

```
$ ws_release MyData
```

For the administrator, the **workspace** is a way to make sure users do not store their data
forever on your fast storage, but have to stage it to slower and cheaper storage.
It also allows migration to new filesystems over time as well as load balancing over several filesystems.
It also allows migration to new filesystems over time as well as load balancing over several
filesystems.

## Documentation

- [Admin Guide](documentation/admin-guide.md) - Installation, configuration, and administration
- [User Guide](documentation/user-guide.md) - End-user documentation for all workspace commands
- [What's New in v2](documenation/whats-new.md) - Overview of changes compared to v1

## Motivation/Goals for a V2

The old v1 codebase got harder and scarier to maintain and needed a major cleanup and modernization.

- Separation of configuration and database implementation from the client tools
is the first goal.

- Better testing is the second goal.

- All python tools will be replaced with C++ tools on the long run.

- backwards compatibility is maintained with some restrictions (e.g. old config files need some
  flags to be added, some minor changes to commands semantics)

- some tools have slightly different options, when it adds functionality or makes usage clearer.

Functional extension and change of DB format is possible after this is achieved.


## Howto Build

### Dependencies

source is fetched and built as part of this tool:
- {fmt}
- glaze
- Guidelines Support Library (GSL)
- spdlog
- bshoshany/thread-pool

library taken from distribution
- boost program_options + boost system
- libcap (optional if capability support is wanted instead of setuid)
- libcurl

for testing:
- Catch2
- bats

### Build commands


#### For production use


```
cmake --preset release
cmake --build --preset release  -j 
```

or for static builds (works only with gcc currently)

```
cmake --preset release-static
cmake --build --preset release-static  -j 
```

##### Distribution remarks

###### Rocky 8

enable some development packages

```
dnf config-manager --enable powertools
dnf install -y gcc-toolset-15
```

and install some dependencies

```
dnf install -y git cmake boost-devel ncurses-devel libcap-devel gcc-c++ libcurl-devel openssl-devel 
```

you will need some SMTP agent.


to build you need a newer compiler than the system compiler:
```
scl enable gcc-toolset-15 bash
```


###### Rocky 9/Rocky 10

enable some development packages

```
dnf config-manager --enable crb
```

and install some dependencies

```
dnf install -y git cmake boost-devel ncurses-devel libcap-devel gcc-c++ libcurl-devel openssl-devel libpsl-devel 
```

you will need some SMTP agent, minimal would be

```
dnf install -y postfix s-nail

```

for debug builds you need in addition

```
dnf install -y libasan libubsan
```




#### For developers


```
cmake --preset debug
cmake --build --preset debug  -j 
```

for mold and ninja users (fastest builds):
```
cmake --preset debug-ninja -DCMAKE_EXE_LINKER_FLAGS="-fuse-ld=mold"
cmake --build --preset debug -j 
```

for lld users (also fast, but slower than mold:
```
cmake --preset debug -DCMAKE_EXE_LINKER_FLAGS="-fuse-ld=lld"
cmake --build --preset debug -j 12
```

to use clang, use
```
cmake --preset debug -DCMAKE_EXE_LINKER_FLAGS="-fuse-ld=lld" -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_C_COMPILER=clang
```

or mix and combine all of above examples.


## Install commands

**TODO**



## Developer informations

### Development Environment

at the moment main development platform is

- Ubuntu 25.10
  - CMake 3.31
  - gcc 15.2.0

test platforms:

- Ubuntu 22.04.5 LTS
  - CMake 3.22.1
  - gcc 11.4.0
  - clang 14.0.0

- Rocky Linux 8.10
  - CMake 3.26.5
  - gcc 14

- Rocky Linux 9.6
  - Cmake 3.26.5
  - gcc 11.5

- Alma Linux 10.1
  - Cmake 3.30.5
  - gcc 14.3.1

this list can be extended.

there is no intention to support old platforms like centos7, but it might work (except ws_stat).

C++23 language level is required (due to glaze YAML support).



### Status

- [x] ws_allocate (C++)
- [x] ws_extend (will remain a shell wrapper)
- [x] ws_find (new in C++)
- [x] ws_list (new in C++)
- [x] ws_register (new in C++)
- [x] ws_release (C++)
- [x] ws_restore (C++)
- [x] ws_send_ical (new in C++)
- [x] ws_editdb (new tool in C++)
- [x] ws_share (will remain a shell script)
- [x] ws_stat (new in C++)
- [x] ws_expirer (migrated to C++)
- [x] ws_validate_config (migrated to C++)
- [x] ws_prepare (new in C++)


### Todo before release candidate

- [x] move from single file ws.conf to multifile ws.d
- migrate config from yaml-cpp to glaze (done)
- [x] move to compiletime+runtime detected capability/setuid/usermode switch (usermode is for testing mainly, does not elevate privileges)
- [x] add more unit tests to existing code
- [x] build/select a better test framework for the tools
- [x] debug what is there
- [x] check debug logic and data leaks
- [x] migrate more tools: migrate ws_expirer, ws_validate_config
- [x] add tests for new tools
- [x] create tests for ws_expire
- [x] debug what is there
- [x] get CMake setup in better shape
- [x] remove tbb dependency
- [x] migrate and check/correct/add documentation, guides and man pages
- [x] test with more compilers and distributions
- [x] ws_editdb manpage
- [x] bash completion
- [x] Cmake fetch for Dependencies
- [x] installation through CMake.
- [-] Add package manager like vcpkg or conan?
- [ ] do real live tests (general behaviour, expirer and ws_stat) and fix bugs on systems with
    - [ ] capabilities
    - [ ] setuid
    - [ ] root_squash
- [ ] review what gets logged for security reasons, should not leak details of other workspaces
- [ ] bash completion testing
- [x] manpage ws_validate_config, ws_prepare

### Future Development
- [ ] define and implement new DB format
- [ ] archiving?

### Input and ideas and contributions needed

- how to automate testing further? 
- missing features in current version?


#### Source Code Formatting

We use [clang-format](https://clang.llvm.org/docs/ClangFormat.html) to ensure consistent code style across the entire
codebase. This helps us to improve readability, avoid bikeshedding, enable automation and make contributions easier.

##### Style Configuration

Our formatting rules are defined in the `.clang-format` file. This configuration file is automatically picked up when
running `clang-format`. We follow a style close to LLVM with some customizations. The style might be tweaked over time.

##### How to Use

We provide a custom CMake target allowing to apply the style configuration to all source files in one sweep:

```
cmake -S . -B build
cmake --build build --target clang-format
```

or - prefered - with presets
```
cmake --build build --preset debug --target clang-format
```

or

```
cmake --build --preset format
```

Using the dry-run option, you can check the compliance of a source code file without applying the style configuration.
This is particularly useful before committing changes.

```
clang-format --dry-run --Werror <file>
```

### Testing

unit tests:

```
ctest --preset debug .
```

higher level tests (user mode only)

```
bats bats/test
```

### testing with docker


```
cd docker
sudo docker build ubuntu-24.04 -t hpcwsv2
sudo docker run hpcwsv2 testall
```

Tests in docker allow coverage analysis

```
cd docker
mkdir coverage
sudo docker run  -v $PWD/coverage:/ws/coverage_report hpcw lcov
sudo chown -R $USERNAME coverage
```

note: setcap tests will fail with ASAN error messages if sysctl `fs.suid_dumpable = 2`
Update: turned out that capability version seems to have restrictions in docker, can only be tested fully in VM
TODO: remove capability tests from docker

### Testing with VMs

Vagrant files are provided to allow testing with rocky linux 8, rocky linux 9 and alma linux 10.
this allows to test settuid and capability version.

```
cd vm/rocky-8
vagrant up
vagrant provision
```

or
```
cd vm/rocky-9
vagrant up
vagrant provision
```

or
```
cd vm/alma-10
vagrant up
vagrant provision
```

It turned out that NFS root_squash and Lustre root_sqash do not behave the same way,
what works on Lustre does not on NFS, so NFS is not suitable for testing.

latest lustre versions also drop capabilites, unless
```
lctl set_param -P mdt.<fsname>-*.enable_cap_mask=+cap_dac_read_search,cap_chown,cap_dac_override,cap_fowner
```
is used.

At the moment there is no solution to test root_squash baviour with this setup.
