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
    local prev
    case "${COMP_WORDS[$COMP_CWORD]}" in
        -*)
            COMPREPLY=($(compgen -W "-F --filesystem -n --name -u --username --delete-data --help --version" -- "${COMP_WORDS[$COMP_CWORD]}"))
            ;;
        *)
            prev="${COMP_WORDS[$COMP_CWORD - 1]}"
            if [[ "$prev" == "-F" || "$prev" == "--filesystem" ]] ; then
                local file_systems="$(_ws_filesystem_list)"
                COMPREPLY=($(compgen -W "$file_systems" -- "${COMP_WORDS[$COMP_CWORD]}"))
            else
                COMPREPLY=($(compgen -W "$(ws_list -s)" -- "${COMP_WORDS[$COMP_CWORD]}"))
            fi
            ;;
    esac
}
complete -F _complete_ws_release ws_release
