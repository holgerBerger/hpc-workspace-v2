#
# bash completion for ws_extend (hpc-workspace)
#
# Copyright(c) 2020-2021 Christoph Niethammer
# Copyright(c) 2026 Holger Berger
#
# SPDX-License-Identifier: GPL-3.0-or-later
#

function _complete_ws_extend() {
    COMPREPLY=($(compgen -W "$(ws_list -s)" -- "${COMP_WORDS[$COMP_CWORD]}"))
}
complete -F _complete_ws_extend ws_extend
