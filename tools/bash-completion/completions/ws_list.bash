#
# bash completion for ws_list (hpc-workspace)
#
# Copyright(c) 2020-2021 Christoph Niethammer
# Copyright(c) 2026 Holger Berger
#
# SPDX-License-Identifier: GPL-3.0-or-later
#

declare -F _ws_filesystem_list >/dev/null ||
    . "${BASH_SOURCE[0]%/*}/../helpers/hpc-workspace.bash" || return

function _complete_ws_list() {
    case "${COMP_WORDS[$COMP_CWORD - 1]}" in
        -F|--filesystem)
            local file_systems="$(_ws_filesystem_list)"
            COMPREPLY=($(compgen -W "$file_systems" -- "${COMP_WORDS[$COMP_CWORD]}"))
            ;;
        -u)
            COMPREPLY=($(compgen -A user -- "${COMP_WORDS[$COMP_CWORD]}"))
            ;;
        *)
            COMPREPLY=($(compgen -W "-a -C -F --filesystem -g -h -l -L -N -r -R -s -t -T -u -v -P --help --version" -- "${COMP_WORDS[$COMP_CWORD]}"))
            ;;
    esac
}
complete -F _complete_ws_list ws_list
