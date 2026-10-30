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
    # name: restorable workspace (from "ws_restore -l -b"), target: existing workspace (from "ws_list -s")
    local prev file_system restorable
    _ws_complete_filesystem && return
    case "${COMP_WORDS[$COMP_CWORD]}" in
        -*)
            COMPREPLY=($(compgen -W "-b --brief --delete-data -F --filesystem -h --help -l --list -n --name -t --target -u --username -V --version" -- "${COMP_WORDS[$COMP_CWORD]}"))
            ;;
        *)
            prev="${COMP_WORDS[$COMP_CWORD - 1]}"
            file_system="$(_ws_option_filesystem)"
            if [[ "$prev" == "--target" || "$prev" == "-t" ]] ; then
                COMPREPLY=($(compgen -W "$(_ws_workspace_list "$file_system")" -- "${COMP_WORDS[$COMP_CWORD]}"))
                return
            fi
            restorable="$(_ws_restore_list "$file_system")"
            if [[ "$prev" == "--name" || "$prev" == "-n" ]] ; then
                COMPREPLY=($(compgen -W "$restorable" -- "${COMP_WORDS[$COMP_CWORD]}"))
            elif [[ $'\n'"$restorable"$'\n' == *$'\n'"$prev"$'\n'* ]] ; then
                # target after the name, but --delete-data takes no target
                [[ " ${COMP_WORDS[*]:1:COMP_CWORD - 1} " == *" --delete-data "* ]] && return
                COMPREPLY=($(compgen -W "$(_ws_workspace_list "$file_system")" -- "${COMP_WORDS[$COMP_CWORD]}"))
            else
                COMPREPLY=($(compgen -W "$restorable" -- "${COMP_WORDS[$COMP_CWORD]}"))
            fi
            ;;
    esac
}
complete -F _complete_ws_restore ws_restore
