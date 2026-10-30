#
# bash completion for ws_share (hpc-workspace)
#
# Copyright(c) 2020-2021, 2026 Christoph Niethammer
# Copyright(c) 2026 Holger Berger
#
# SPDX-License-Identifier: GPL-3.0-or-later
#

declare -F _ws_filesystem_list >/dev/null ||
    . "${BASH_SOURCE[0]%/*}/../helpers/hpc-workspace.bash" || return

function _complete_ws_share() {
    local action file_system ws_name i
    local -a args=() fs_opt=()
    _ws_complete_filesystem && return
    case "${COMP_WORDS[$COMP_CWORD]}" in
        -*)
            COMPREPLY=($(compgen -W "-F --filesystem -h --help --readwrite --version" -- "${COMP_WORDS[$COMP_CWORD]}"))
            ;;
        *)
            # positional arguments before the current word: action, workspace name, users or groups.
            # ws_share parses its options with getopt, so they can appear anywhere; -F and --config take a value.
            for (( i = 1; i < COMP_CWORD; i++ )); do
                case "${COMP_WORDS[i]}" in
                    -F|--filesystem|--config)
                        (( i++ ))
                        ;;
                    -*)
                        ;;
                    *)
                        args+=("${COMP_WORDS[i]}")
                        ;;
                esac
            done
            declare -i argpos=${#args[@]}
            if [[ $argpos -eq 0 ]] ; then
                COMPREPLY=($(compgen -W "share unshare unshare-all unsharegroup sharegroup list" -- "${COMP_WORDS[$COMP_CWORD]}"))
            else
                action="${args[0]}"
                ws_name="${args[1]}"
                file_system="$(_ws_option_filesystem)"
                case $action in
                    share)
                        case $argpos in
                            1)
                                local ws_names=$(_ws_workspace_list "$file_system")
                                COMPREPLY=($(compgen -W "$ws_names" -- "${COMP_WORDS[$COMP_CWORD]}"))
                                ;;
                            *)
                                COMPREPLY=($(compgen -A user -- "${COMP_WORDS[$COMP_CWORD]}"))
                                ;;
                        esac
                        ;;
                    unshare)
                        case $argpos in
                            1)
                                local ws_names=$(_ws_workspace_list "$file_system")
                                COMPREPLY=($(compgen -W "$ws_names" -- "${COMP_WORDS[$COMP_CWORD]}"))
                                ;;
                            *)
                                [[ -n "$file_system" ]] && fs_opt=(-F "$file_system")
                                # users the workspace is shared with, "ws_share list" prints lines "user|group <name> <access>"
                                local shared_users="$(ws_share list "${fs_opt[@]}" "$ws_name" 2>/dev/null | awk '$1 == "user" { print $2 }')"
                                COMPREPLY=($(compgen -W "$shared_users" -- "${COMP_WORDS[$COMP_CWORD]}"))
                                ;;
                        esac
                        ;;
                    unshare-all|list)
                        if [[ $argpos -le 1 ]] ; then
                            local ws_names=$(_ws_workspace_list "$file_system")
                            COMPREPLY=($(compgen -W "$ws_names" -- "${COMP_WORDS[$COMP_CWORD]}"))
                        else
                            COMPREPLY=()
                        fi
                        ;;
                    unsharegroup|sharegroup)
                        if [[ $argpos -le 1 ]] ; then
                            local ws_names=$(_ws_workspace_list "$file_system")
                            COMPREPLY=($(compgen -W "$ws_names" -- "${COMP_WORDS[$COMP_CWORD]}"))
                        else
                            COMPREPLY=()
                        fi
                        ;;
                esac
            fi
            ;;
    esac
}
complete -F _complete_ws_share ws_share
