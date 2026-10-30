#
# bash completion for ws_release (hpc-workspace)
#
# Copyright(c) 2020-2021 Christoph Niethammer
# Copyright(c) 2026 Holger Berger
#
# SPDX-License-Identifier: GPL-3.0-or-later
#

declare -F _ws_filesystem_list >/dev/null ||
    . "${BASH_SOURCE[0]%/*}/../helpers/hpc-workspace.bash" || return

function _complete_ws_release() {
    _ws_complete_filesystem && return
    case "${COMP_WORDS[$COMP_CWORD]}" in
        -*)
            COMPREPLY=($(compgen -W "-F --filesystem -h --help -n --name -u --username -V --version --config --delete-data" -- "${COMP_WORDS[$COMP_CWORD]}"))
            ;;
        *)
            COMPREPLY=($(compgen -W "$(_ws_workspace_list "$(_ws_option_filesystem)")" -- "${COMP_WORDS[$COMP_CWORD]}"))
            ;;
    esac
}
complete -F _complete_ws_release ws_release
