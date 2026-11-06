#!/usr/bin/env bash
# Builds libchisn_dpi.so: the ChiSnCore CHI Subordinate-Node functional model
# (synced from CUTE-Test) plus its DPI glue, for the L2SN testbench
# (vcs/tb/vcs_tb_l2sn.sv). Loaded at runtime alongside libtltest_dpi.so via
# the run_vcs.sh TLTEST_VCS_EXTRA_DPI_LIBS hook.
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
out_dir=${TLTEST_CHISN_BUILD_DIR:-$root/build_vcs/dpi}
export VCS_HOME=${VCS_HOME:-/nfs/tools/synopsys/vcs/Q-2020.03-SP2}

mkdir -p "$out_dir"
g++ -std=c++17 -O2 -fPIC -shared \
  -I"$root/vcs/chi_sn/include" -I"$VCS_HOME/include" \
  "$root/vcs/chi_sn/chi_sn_core.cpp" \
  "$root/vcs/chi_sn/chi_sn_dpi.cpp" \
  -o "$out_dir/libchisn_dpi.so"
echo "built $out_dir/libchisn_dpi.so"
