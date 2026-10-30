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
    local i
    _ws_complete_filesystem && return
    case "${COMP_WORDS[$COMP_CWORD - 1]}" in
        -n|--name)
            COMPREPLY=($(compgen -W "$(_ws_workspace_list "$(_ws_option_filesystem)")" -- "${COMP_WORDS[$COMP_CWORD]}"))
            return
            ;;
        -u|--username|--config)
            return
            ;;
    esac
    case "${COMP_WORDS[$COMP_CWORD]}" in
        -*)
            COMPREPLY=($(compgen -W "-F --filesystem -g -h -u --help --version" -- "${COMP_WORDS[$COMP_CWORD]}"))
            ;;
        *)
            # ws_find takes exactly one workspace name, as argument or with -n; options can appear anywhere,
            # -F, -u, -n and --config take a value
            for (( i = 1; i < COMP_CWORD; i++ )); do
                case "${COMP_WORDS[i]}" in
                    -n|--name|-n?*)
                        return
                        ;;
                    -F|--filesystem|-u|--username|--config)
                        (( i++ ))
                        ;;
                    -*)
                        ;;
                    *)
                        return
                        ;;
                esac
            done
            COMPREPLY=($(compgen -W "$(_ws_workspace_list "$(_ws_option_filesystem)")" -- "${COMP_WORDS[$COMP_CWORD]}"))
            ;;
    esac
}
complete -F _complete_ws_find ws_find
