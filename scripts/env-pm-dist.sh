#!/usr/bin/env bash
# Prepend packages/media/cpp/dist to PATH (absolute). Use from any shell:
#   source "$(dirname "$0")/env-pm-dist.sh"     # relative
#   source /path/to/packages/media/cpp/scripts/env-pm-dist.sh
_pm_cpp_root="$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")/.." && pwd)"
export PATH="${_pm_cpp_root}/dist:${PATH}"
unset _pm_cpp_root
