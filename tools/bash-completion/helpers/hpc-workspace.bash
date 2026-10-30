#
# bash completion for hpc-workspace: shared helper functions
#
# Sourced by the per-tool completion files in ../completions. Installed into
# the bash-completion helpers directory, which the loader never searches, so
# it cannot be mistaken for the completion of a command.
#
# Copyright(c) 2020-2021 Christoph Niethammer
# Copyright(c) 2026 Holger Berger
#
# bash completion for hpc-workspace is free software: you can redistribute it
# and/or modify it under the terms of the GNU General Public License as
# published by the Free Software Foundation, either version 3 of the License,
# or (at your option) any later version.
#
# bash completion for hpc-workspace is distributed in the hope that it will
# be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General
# Public License for more details.
#
# You should have received a copy of the GNU General Public License along with
# bash completion for hpc-workspace.  If not, see <http://www.gnu.org/licenses/>.
#

function _ws_filesystem_list() {
    local file_systems="$(ws_list -l 2>/dev/null | tail -n +1 | grep -v "available filesystems" | cut -f 1 -d ' ')"
    printf "%s" "$file_systems"
}

function _ws_workspace_list() {
    local file_system="$1"
    local ws_names
    if [ "$file_system" != "" ] ; then
        ws_names="$(ws_list -F "$file_system" -s 2>/dev/null)"
    else
        ws_names="$(ws_list -s 2>/dev/null)"
    fi
    printf "%s" "$ws_names"
}

function _ws_restore_list() {
    local restore_targets="$(ws_restore -l -b 2>/dev/null | grep -E -v ":$")"
    printf "%s" "$restore_targets"
}
