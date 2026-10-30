#
# bash completion for ws_extend (hpc-workspace)
#
# Copyright(c) 2020-2021 Christoph Niethammer
# Copyright(c) 2026 Holger Berger
#
# SPDX-License-Identifier: GPL-3.0-or-later
#

declare -F _ws_filesystem_list >/dev/null ||
    . "${BASH_SOURCE[0]%/*}/../helpers/hpc-workspace.bash" || return

function _complete_ws_extend() {
    _ws_complete_filesystem && return
    # the ws_extend wrapper accepts -F only as first argument
    if [[ "$COMP_CWORD" == "1" && "${COMP_WORDS[1]}" == -* ]] ; then
        COMPREPLY=($(compgen -W "-F" -- "${COMP_WORDS[1]}"))
        return
    fi
    COMPREPLY=($(compgen -W "$(_ws_workspace_list "$(_ws_option_filesystem)")" -- "${COMP_WORDS[$COMP_CWORD]}"))
}
complete -F _complete_ws_extend ws_extend
