#
# bash completion for ws_restore (hpc-workspace)
#
# Copyright(c) 2020-2021 Christoph Niethammer
# Copyright(c) 2026 Holger Berger
#
# SPDX-License-Identifier: GPL-3.0-or-later
#

declare -F _ws_filesystem_list >/dev/null ||
    . "${BASH_SOURCE[0]%/*}/../helpers/hpc-workspace.bash" || return

function _complete_ws_restore() {
    local prev
    case "${COMP_WORDS[$COMP_CWORD]}" in
        -*)
            COMPREPLY=($(compgen -W "--brief --delete-data --filesystem --help --list --name --target --username --version" -- "${COMP_WORDS[$COMP_CWORD]}"))
            ;;
        *)
            prev="${COMP_WORDS[$COMP_CWORD - 1]}"
            local restore_targets="$(_ws_restore_list)"
            if [[ "$prev" == "-F" || "$prev" == "--filesystem" ]] ; then
                local file_systems="$(_ws_filesystem_list)"
                COMPREPLY=($(compgen -W "$file_systems" -- "${COMP_WORDS[$COMP_CWORD]}"))
            elif [[ "$prev" == "--name" || "$prev" == "-n" || " $restore_targets " == *" $prev "* ]] ; then
                COMPREPLY=($(compgen -W "$(ws_list -s)" -- "${COMP_WORDS[$COMP_CWORD]}"))
            else
                COMPREPLY=($(compgen -W "$restore_targets" -- "${COMP_WORDS[$COMP_CWORD]}"))
            fi
            ;;
    esac
}
complete -F _complete_ws_restore ws_restore
