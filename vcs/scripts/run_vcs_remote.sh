#!/usr/bin/env bash
# Run a Makefile target on the EDA host (default: eda01) from any machine.
#
#   vcs/scripts/run_vcs_remote.sh <make target> [VAR=value ...]
#   vcs/scripts/run_vcs_remote.sh                 # defaults to vcs-run
#
# Already on that host: runs locally. Otherwise ssh's over and runs it there.
# The remote exit status is forwarded; TLTEST_* variables are passed through.
set -euo pipefail

remote=${TLTEST_EDA_HOST:-eda01}
modules=${TLTEST_EDA_MODULES:-license synopsys/vcs/Q-2020.03-SP2}
devtoolset=${TLTEST_DEVTOOLSET:-devtoolset-11}
project_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)

args=("$@")
((${#args[@]})) || args=(vcs-run)

if [[ "$(hostname -s)" == "${remote#*@}" ]]; then
  if command -v scl >/dev/null 2>&1; then
    exec scl enable "$devtoolset" -- make -C "$project_root" "${args[@]}"
  fi
  exec make -C "$project_root" "${args[@]}"
fi

env_prefix=
while IFS= read -r v; do
  env_prefix+="$v=$(printf %q "${!v}") "
done < <(compgen -e | grep '^TLTEST_' || true)

cmd="cd $(printf %q "$project_root")"
for m in $modules; do
  cmd+=" && module load $m"
done
cmd+=" && if command -v scl >/dev/null 2>&1; then"
cmd+=" exec env $env_prefix scl enable $devtoolset -- make$(printf ' %q' "${args[@]}");"
cmd+=" else exec env $env_prefix make$(printf ' %q' "${args[@]}"); fi"

exec ssh -T -o BatchMode=yes -o ConnectTimeout=10 -o LogLevel=ERROR \
  -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=accept-new \
  "$remote" "bash -lc $(printf %q "$cmd")"
