Name:    hpc-workspace-v2
Version: 2.0.0
Release: 1%{?dist}
Summary: A workspace is a directory created on behalf of a user, associated with an expiration date, to prevent disks from uncontrolled filling. The project provides user and admin tools to manage those directories.

Group:   System
License: GPL-3.0-or-later
URL:     https://github.com/holgerBerger/hpc-workspace-v2
# The tarball top-level directory must match %%{name}-%%{version}
Source0: %{url}/archive/refs/tags/v%{version}/%{name}-%{version}.tar.gz

BuildRequires: gcc-c++
BuildRequires: cmake >= 3.20
BuildRequires: make
BuildRequires: boost-devel
BuildRequires: libcurl-devel
# Required for the capability build 
BuildRequires: libcap-devel

%description
A workspace is a directory created on behalf of a user, associated with an expiration date, to prevent disks from uncontrolled filling. The project provides user and admin tools to manage those directories.

%prep
%autosetup -n %{name}-%{version}


%build

%cmake \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DBUILD_SHARED_LIBS=OFF \
    -DWS_USE_CAPABILITIES=ON \
    -DWS_INSTALL_SET_PRIVILEGES=OFF \
    -DBUILD_TESTS=OFF

%cmake_build

%install
%cmake_install

%files
%license LICENSE
%doc README.md documentation/admin-guide.md documentation/user-guide.md
%doc config-minimal-sample.conf config-full-sample.conf

# Privileged user tools.
%caps(cap_dac_override,cap_chown,cap_fowner=p) %{_bindir}/ws_allocate
%caps(cap_dac_override,cap_chown,cap_fowner=p) %{_bindir}/ws_release
%caps(cap_dac_override,cap_dac_read_search=p) %{_bindir}/ws_restore

# Alternative: setuid root instead of capabilities. To use this, drop the three
# %caps lines above, replace them with the three lines below, and build with
# -DWS_USE_CAPABILITIES=OFF (also drop the libcap-devel BuildRequires).
# %attr(4755,root,root) %{_bindir}/ws_allocate
# %attr(4755,root,root) %{_bindir}/ws_release
# %attr(4755,root,root) %{_bindir}/ws_restore

# Unprivileged user tools.
%{_bindir}/ws_list
%{_bindir}/ws_find
%{_bindir}/ws_register
%{_bindir}/ws_send_ical
%{_bindir}/ws_stat
%{_bindir}/ws_validate_config
%{_bindir}/ws_extend
%{_bindir}/ws_share

# Administrator tools.
%{_sbindir}/ws_expirer
%{_sbindir}/ws_editdb
%{_sbindir}/ws_prepare

# Manual pages.
%{_mandir}/man1/ws_*.1*
%{_mandir}/man8/ws_*.8*


%changelog
* Thu Jun 18 2026 Maria Hampel <maria.hampel@mailbox.tu-dresden.de> - 2.0.0-1
- Initial RPM
