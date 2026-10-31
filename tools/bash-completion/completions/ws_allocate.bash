#
# bash completion for ws_allocate (hpc-workspace)
#
# Copyright(c) 2026 Christoph Niethammer
#
# SPDX-License-Identifier: GPL-3.0-or-later
#

declare -F _ws_filesystem_list >/dev/null ||
    . "${BASH_SOURCE[0]%/*}/../helpers/hpc-workspace.bash" || return

function _complete_ws_allocate() {
    local i word extension=""
    _ws_complete_filesystem && return
    # the name of a new workspace cannot be completed, only an existing one to extend with -x
    for word in "${COMP_WORDS[@]}"; do
        [[ "$word" == "-x" || "$word" == "--extension" ]] && extension=1
    done
    case "${COMP_WORDS[$COMP_CWORD - 1]}" in
        -u|--username)
            COMPREPLY=($(compgen -A user -- "${COMP_WORDS[$COMP_CWORD]}"))
            return
            ;;
        -n|--name)
            [[ -n "$extension" ]] &&
                COMPREPLY=($(compgen -W "$(_ws_workspace_list "$(_ws_option_filesystem)")" -- "${COMP_WORDS[$COMP_CWORD]}"))
            return
            ;;
        -d|--duration|-r|--reminder|-m|--mailaddress|-c|--comment|--config)
            return
            ;;
    esac
    case "${COMP_WORDS[$COMP_CWORD]}" in
        -*)
            COMPREPLY=($(compgen -W "-h --help -V --version -d --duration -n --name -F --filesystem -r --reminder -m --mailaddress -x --extension -u --username -g --groupreadable -G --groupwritable -c --comment" -- "${COMP_WORDS[$COMP_CWORD]}"))
            ;;
        *)
            [[ -n "$extension" ]] || return
            # arguments: workspace name and duration; options can appear anywhere.
            # -g and -G take their optional group only attached (-ggroup), so they do not consume the next word.
            for (( i = 1; i < COMP_CWORD; i++ )); do
                case "${COMP_WORDS[i]}" in
                    -n|--name|-n?*)
                        return
                        ;;
                    -d|--duration|-F|--filesystem|-r|--reminder|-m|--mailaddress|-u|--username|-c|--comment|--config)
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
complete -F _complete_ws_allocate ws_allocate
