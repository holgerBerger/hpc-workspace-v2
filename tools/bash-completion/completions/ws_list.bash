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
    _ws_complete_filesystem && return
    case "${COMP_WORDS[$COMP_CWORD - 1]}" in
        -u|--username)
            COMPREPLY=($(compgen -A user -- "${COMP_WORDS[$COMP_CWORD]}"))
            ;;
        -p|--pattern|-n|--threads|--config)
            ;;
        *)
            COMPREPLY=($(compgen -W "-h --help -V --version -F --filesystem -g --group -l --listfilesystems -L --listfilesystemdetails -s --short -u --username -e --expired -N --name -C --creation -R --remaining -r --reverted -t --terse -T --table -p --pattern -P --permissions -v --verbose -n --threads" -- "${COMP_WORDS[$COMP_CWORD]}"))
            ;;
    esac
}
complete -F _complete_ws_list ws_list
