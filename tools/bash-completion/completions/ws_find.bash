#
# bash completion for ws_find (hpc-workspace)
#
# Copyright(c) 2020-2021 Christoph Niethammer
# Copyright(c) 2026 Holger Berger
#
# SPDX-License-Identifier: GPL-3.0-or-later
#

declare -F _ws_filesystem_list >/dev/null ||
    . "${BASH_SOURCE[0]%/*}/../helpers/hpc-workspace.bash" || return

function _complete_ws_find() {
    local file_system=""
    _ws_complete_filesystem && return
    case "${COMP_WORDS[$COMP_CWORD]}" in
        -*)
            [[ "$COMP_CWORD" != "1" ]] && return
            COMPREPLY=($(compgen -W "-F --filesystem -g -h -u --help --version" -- "${COMP_WORDS[$COMP_CWORD]}"))
            ;;
        *)
            if [[ "${COMP_WORDS[1]}" == "-F" || "${COMP_WORDS[1]}" == "--filesystem" ]] ; then
                file_system="${COMP_WORDS[2]}"
            fi
            [[ ! ("$COMP_CWORD" == "1" || ("$file_system" != "" && "$COMP_CWORD" == "3")) ]] && return
            local ws_names=$(_ws_workspace_list "$file_system")
            COMPREPLY=($(compgen -W "$ws_names" -- "${COMP_WORDS[$COMP_CWORD]}"))
            ;;
    esac
}
complete -F _complete_ws_find ws_find
