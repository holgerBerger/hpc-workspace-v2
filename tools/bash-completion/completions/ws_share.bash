#
# bash completion for ws_share (hpc-workspace)
#
# Copyright(c) 2020-2021 Christoph Niethammer
# Copyright(c) 2026 Holger Berger
#
# SPDX-License-Identifier: GPL-3.0-or-later
#

declare -F _ws_filesystem_list >/dev/null ||
    . "${BASH_SOURCE[0]%/*}/../helpers/hpc-workspace.bash" || return

function _complete_ws_share() {
    local action file_system="" ws_name filesysopt
    _ws_complete_filesystem && return
    case "${COMP_WORDS[$COMP_CWORD]}" in
        -*)
            COMPREPLY=($(compgen -W "-F --filesystem -h --help" -- "${COMP_WORDS[$COMP_CWORD]}"))
            ;;
        *)
            if [[ "$COMP_CWORD" == "1" ]] ; then
                COMPREPLY=($(compgen -W "share unshare unshare-all unsharegroup sharegroup list" -- "${COMP_WORDS[$COMP_CWORD]}"))
            else
                action="${COMP_WORDS[1]}"
                declare -i argpos=$((COMP_CWORD - 1))
                if [[ "${COMP_WORDS[2]}" == "-F" || "${COMP_WORDS[2]}" == "--filesystem" ]] ; then
                    file_system="${COMP_WORDS[3]}"
                    argpos=$((argpos - 2))
                fi
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
                                ws_name="${COMP_WORDS[$((COMP_CWORD - argpos + 1))]}"
                                filesysopt="${file_system:+-F $file_system}"
                                COMPREPLY=($(compgen -W "$(ws_share list $filesysopt $ws_name 2>/dev/null)" -- "${COMP_WORDS[$COMP_CWORD]}"))
                                ;;
                        esac
                        ;;
                    unshare-all|list)
                        if [[ $argpos -le 1 ]] ; then
                            local ws_names=$(_ws_workspace_list "$file_system")
                            COMPREPLY=($(compgen -W "$ws_names" -- "${COMP_WORDS[$COMP_CWORD]}"))
                        else
                            COMPREPLY=""
                        fi
                        ;;
                    unsharegroup|sharegroup)
                        if [[ $argpos -le 1 ]] ; then
                            local ws_names=$(_ws_workspace_list "$file_system")
                            COMPREPLY=($(compgen -W "$ws_names" -- "${COMP_WORDS[$COMP_CWORD]}"))
                        else
                            COMPREPLY=""
                        fi
                        ;;
                esac
            fi
            ;;
    esac
}
complete -F _complete_ws_share ws_share
