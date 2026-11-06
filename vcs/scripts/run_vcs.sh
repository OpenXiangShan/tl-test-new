#!/usr/bin/env bash
# VCS backend for the XSAICache matrix trace replay (tl-test-new DPI Guest
# Mode). Expects the TestTop from 'make -C dut/XSCache test-top-matrix
# NUM_CORE=1 NUM_SLICE=1' (matches configs/coupledL2-test-matrix-trace).
#
# Usage:
#   vcs/scripts/run_vcs.sh build                  compile simv (no coverage instrumentation)
#   vcs/scripts/run_vcs.sh run                    compile if needed, replay the trace in
#                                         main/build/tltest.ini (the make-generated one)
#   vcs/scripts/run_vcs.sh coverage-prepare       prepare shared coverage compiler cache only
#   vcs/scripts/run_vcs.sh coverage [trace]       coverage run of one trace -> own vdb + report
#                                         trace: a file under traces/ (extension optional,
#                                         .ops/.trace/.txt tried in turn); TLTEST_VCS_TRACE=<path>
#                                         overrides. Without an argument the trace configured
#                                         in the ini is replayed and labeled matrix_trace.
#                                         simv is compiled once and reused;
#                                         TLTEST_VCS_REBUILD=1 forces recompilation.
#   vcs/scripts/run_vcs.sh coverage-fuzz <name> <ini>
#                                         random-stimulus coverage run (no trace); the ini
#                                         (e.g. a FUZZ_ARI config) drives everything, name
#                                         labels the vdb/report
#   vcs/scripts/run_vcs.sh coverage-case <name> <ini> <trace-path>
#                                         named trace run with an explicit INI
#   vcs/scripts/run_vcs.sh coverage-merge         urg-merge every vdb in the coverage output directory
#
# TLTEST_VCS_COVERAGE_DIR overrides the VDB/report/log output directory without
# changing the compile cache at TLTEST_VCS_BUILD_DIR/coverage.
# TLTEST_VCS_RUN_ROOT overrides the simulation directory prefix (each named run
# uses <prefix>_<name>). Defaults retain the existing build_vcs and run layout.
# TLTEST_VCS_TIMEOUT optionally limits the simulator runtime in positive seconds.
#
# TLTEST_CLOGB_DISABLE=1 bypasses the CLog.B record pipeline (per-record
# heap alloc + queue + file I/O dominate wall time on transaction-heavy
# runs). The .clog.b file stays empty; coverage numbers are unaffected
# (validated on the HBL2 TestTop 2026-10-26: Line/Cond/FSM identical).
#
# Modeled on CUTE-Test/vcs/run_vcs.sh. The VCS Q-2020.03 launcher scripts carry
# a '#!/bin/sh -h' shebang that dash rejects, and default to the 32-bit
# binaries; both are worked around by invoking them through bash with -full64.
set -euo pipefail

project_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
rtl_dir_default="$project_root/dut/XSCache/build/openllc"
rtl_dir=${TLTEST_RTL_DIR:-$rtl_dir_default}
# generated C++ companions (chisel_db.cpp / perfCCT.cpp) live one level above
# the split-verilog directory
db_dir=$(cd "$(dirname "$rtl_dir")" && pwd)
vcs_build_root=${TLTEST_VCS_BUILD_DIR:-$project_root/build_vcs}
run_root=${TLTEST_VCS_RUN_ROOT:-$project_root/run/vcs_run}
coverage_output_dir=${TLTEST_VCS_COVERAGE_DIR:-$vcs_build_root/coverage}
# Output overrides are relative to the project root, even when simv changes cwd.
[[ "$run_root" == /* ]] || run_root="$project_root/$run_root"
[[ "$coverage_output_dir" == /* ]] || coverage_output_dir="$project_root/$coverage_output_dir"
# TLTEST_VCS_TB swaps the testbench (e.g. vcs/tb/vcs_tb_l2sn.sv for the
# L2-only TestTopMatrixL2SN + ChiSnCore SN backend).
tb_file=${TLTEST_VCS_TB:-$project_root/vcs/tb/vcs_tb.sv}
coverage_hier="$project_root/vcs/config/coverage.hier"
dpi_lib="$project_root/main/build/libtltest_dpi.so"
mode=${1:-run}

export VCS_HOME=${VCS_HOME:-/nfs/tools/synopsys/vcs/Q-2020.03-SP2}
export LM_LICENSE_FILE=${LM_LICENSE_FILE:-28000@eda01}
vcs_cpp=${TLTEST_VCS_CPP:-/usr/bin/g++}
vcs_jobs=${TLTEST_VCS_JOBS:-16}

vcs() { bash "$VCS_HOME/bin/vcs" -full64 "$@"; }
# urg's wrapper invokes ${VCS_HOME}/bin/vcs -location directly, so the
# dash-rejected '#!/bin/sh -h' shebang (see the header comment) breaks it
# from the inside — wrapping urg in bash is not enough. Patch a build-dir
# copy: bash shebang plus routing the internal vcs -location / vcsMsgReport
# calls through bash. -full64 is forced to skip the vdb auto-detect branch.
urg() (
  # Each parallel report process owns its launcher; no process truncates a
  # wrapper another process is about to execute.
  mkdir -p "$vcs_build_root/tool"
  local urg_wrapper
  urg_wrapper=$(mktemp "$vcs_build_root/tool/urg.XXXXXX")
  trap 'rm -f "$urg_wrapper"' EXIT
  sed -e '1s|^#!.*$|#!/usr/bin/env bash|' \
      -e 's|\${VCS_HOME}/bin/vcs -location|bash ${VCS_HOME}/bin/vcs -location|g' \
      -e 's|ERR_MSG_REPORT=${BASE_STRING}/bin/vcsMsgReport|ERR_MSG_REPORT="bash ${BASE_STRING}/bin/vcsMsgReport"|' \
      "$VCS_HOME/bin/urg" > "$urg_wrapper"
  chmod +x "$urg_wrapper"
  bash "$urg_wrapper" -full64 "$@"
)

require_dpi_lib() {
  if [[ ! -f "$dpi_lib" ]]; then
    echo "error: $dpi_lib is missing; run 'make tltest-build-dpi-coupledL2' first" >&2
    exit 2
  fi
}

require_rtl() {
  if [[ ! -f "$rtl_dir/TestTop.sv" || ! -f "$rtl_dir/filelist.f" ]]; then
    echo "error: generated RTL is missing at $rtl_dir; run" >&2
    echo "       'make -C dut/XSCache test-top-matrix NUM_CORE=1 NUM_SLICE=1' first" >&2
    exit 2
  fi
}

# Blackbox / behavioral models that live next to the generated RTL but are not
# referenced by filelist.f (CLogB_Stub*.sv, TLLogWriter.v, ClockGate.sv, ...).
# VCS resolves nothing on its own, so every one of them must be on the command
# line — the Verilator flow gets them implicitly from its build/*.*v glob.
collect_ext_models() {
  local dir=$1
  local f
  (
    cd "$dir" || exit 1
    for f in *.sv *.v; do
      [[ -e "$f" ]] || continue
      grep -qx "$f" filelist.f || printf '%s\n' "$f"
    done
  )
}

# compile_simv BUILD_DIR COVERAGE(on|off) COMPILE_RTL_DIR
compile_simv() {
  local build_dir=$1
  local coverage=$2
  local compile_rtl_dir=$3
  local simv_path

  mkdir -p "$build_dir"
  simv_path=$(cd "$build_dir" && pwd)/simv
  # VCS does not track C++ header dependencies in its -Mdir cache.
  rm -rf "$build_dir/csrc"

  mapfile -t ext_models < <(collect_ext_models "$compile_rtl_dir")

  # chisel_db.cpp composes its INSERT statements with %#lx (0x-prefixed hex),
  # which SQLite only accepts as an integer literal from 3.8.6 on. The simv
  # link resolves libsqlite3 to the ancient copy bundled inside VCS_HOME, which
  # rejects the 0x tokens. Patch a build-dir copy to decimal %lu so the stored
  # integers are identical on any SQLite version. ($db_dir = the parent of
  # the original rtl dir; the coverage-rtl copy does not include these.)
  sed 's/%#lx/%lu/g' "$db_dir/chisel_db.cpp" > "$build_dir/chisel_db.cpp"

  local -a command=(
    -cpp "$vcs_cpp" -CFLAGS "-std=c++11 -I$compile_rtl_dir -I$db_dir"
    -sverilog -timescale=1ns/1ps
    +vcs+initreg+random
    -debug_access+all
    -j "$vcs_jobs"
  )
  if [[ "$coverage" == on ]]; then
    command+=(-cm line+cond+fsm+tgl -cm_hier "$coverage_hier"
              -cm_dir "$build_dir/template.vdb")
  fi
  local -a db_srcs=("$build_dir/chisel_db.cpp" "$db_dir/perfCCT.cpp")
  [[ -f "$db_dir/constantin.cpp" ]] && db_srcs+=("$db_dir/constantin.cpp")
  command+=(-f filelist.f "${ext_models[@]}"
            "${db_srcs[@]}"
            "$tb_file" +incdir+"$project_root/main/DPI"
            -top tltest_vcs_tb -Mdir="$build_dir/csrc" -o "$simv_path"
            -LDFLAGS "-lsqlite3 -ldl"
            -l "$build_dir/compile.log")

  echo "VCS compile in $compile_rtl_dir -> $simv_path"
  (cd "$compile_rtl_dir" && vcs "${command[@]}")
}

# Serialize compiler/cache mutation. Parallel simulations only read simv and
# template.vdb after this function has completed successfully.
ensure_coverage_cache() (
  require_rtl
  require_dpi_lib
  local build_dir="$vcs_build_root/coverage"
  local coverage_rtl_dir="$build_dir/rtl"
  mkdir -p "$build_dir"
  # A subshell limits the lock descriptor lifetime to cache preparation.
  exec 9>"$build_dir/.compile.lock"
  flock -x 9
  if [[ ! -f "$build_dir/simv" || ! -d "$build_dir/template.vdb" || "${TLTEST_VCS_REBUILD:-0}" == 1 ]]; then
    bash "$project_root/vcs/scripts/prepare_coverage_rtl.sh" "$rtl_dir" "$coverage_rtl_dir"
    compile_simv "$build_dir" on "$coverage_rtl_dir"
  fi
)

# run_simv SIMV COVERAGE_DIR(empty for plain runs) TRACE_FILE(empty for ini default) RUN_SUFFIX
run_simv() {
  local simv=$1
  local coverage_dir=$2
  local trace_file=$3
  local run_suffix=${4:-}
  local this_run_root="$run_root${run_suffix:+_$run_suffix}"
  local log_file="$this_run_root/simv.log"
  local ini_file="${TLTEST_VCS_INI:-$project_root/main/build/tltest.ini}"

  require_dpi_lib
  rm -rf "$this_run_root" 2>/dev/null || true
  mkdir -p "$this_run_root"
  # TileLinkSystemInitialize() reads tltest.ini from the working directory;
  # the trace path inside may be relative to the repo root, so rewrite it to
  # the absolute path of the trace this run should replay (if any).
  # TLTEST_VCS_INI swaps in a different config (e.g. a FUZZ_ARI ini for
  # random-stimulus runs that have no trace at all).
  if [[ ! -f "$ini_file" ]]; then
    echo "error: ini not found: $ini_file (run a make tltest-config-* target first)" >&2
    exit 2
  fi
  cp "$ini_file" "$this_run_root/tltest.ini"
  if [[ -n "$trace_file" ]]; then
    local trace_replacement=${trace_file//\\/\\\\}
    trace_replacement=${trace_replacement//&/\\&}
    trace_replacement=${trace_replacement//#/\\#}
    sed -i -E "s#^[[:space:]]*trace.filepath[[:space:]]*=.*#trace.filepath              = $trace_replacement#" \
      "$this_run_root/tltest.ini"
  fi
  printf '#!SV_LIBRARIES\n%s\n' "${dpi_lib%.so}" > "$this_run_root/bootstrap_lib_files"
  # Space-separated extra DPI shared objects (e.g. the ChiSnCore SN model for
  # the L2SN testbench, see vcs/scripts/build_chisn_dpi.sh), one per line.
  local extra_lib
  for extra_lib in ${TLTEST_VCS_EXTRA_DPI_LIBS:-}; do
    printf '%s\n' "${extra_lib%.so}" >> "$this_run_root/bootstrap_lib_files"
  done

  # Match the Verilator zero-initialization semantics: registers and memory
  # arrays start at 0 instead of X (chosen at runtime; the compile only
  # enables the mechanism — +vcs+initreg covers both).
  local -a run_args=(-sv_liblist bootstrap_lib_files +vcs+initreg+0)
  # Extra runtime plusargs (e.g. "+vcd_start=40500 +vcd_stop=42000"), split
  # on spaces so callers can hand whole plusarg groups through the env.
  if [[ -n "${TLTEST_VCS_RUN_ARGS:-}" ]]; then
    read -r -a extra_args <<< "$TLTEST_VCS_RUN_ARGS"
    run_args+=("${extra_args[@]}")
  fi
  if [[ -n "$coverage_dir" ]]; then
    run_args+=(-cm line+cond+fsm+tgl -cm_dir "$coverage_dir" -cm_name "$run_suffix")
  fi

  local timeout_seconds=${TLTEST_VCS_TIMEOUT:-}
  local -a simv_command=("$simv" "${run_args[@]}")
  if [[ -n "$timeout_seconds" ]]; then
    if [[ ! "$timeout_seconds" =~ ^[1-9][0-9]*$ ]]; then
      echo "error: TLTEST_VCS_TIMEOUT must be a positive integer in seconds" >&2
      exit 2
    fi
    simv_command=(timeout --signal=TERM --kill-after=10s "${timeout_seconds}s" "${simv_command[@]}")
  fi

  echo "VCS run in $this_run_root (log: $log_file)"
  (cd "$this_run_root" && "${simv_command[@]}") 2>&1 | tee "$log_file" || {
    local run_status=$?
    if [[ "$run_status" -eq 124 ]]; then
      echo "error: VCS simulation timed out after ${timeout_seconds}s; see $log_file" >&2
    else
      echo "error: VCS exited unsuccessfully; see $log_file" >&2
    fi
    exit 1
  }

  # This VCS version reports $fatal with a zero process exit status, so the
  # simulator log is the authoritative result.
  if grep -q 'Fatal:' "$log_file"; then
    echo "error: VCS test failed; see $log_file" >&2
    exit 1
  fi
  if ! grep -q 'FINISHED' "$log_file"; then
    echo "error: replay did not reach FINISHED; see $log_file" >&2
    exit 1
  fi
  grep -m1 'FINISHED' "$log_file" || true
}

case "$mode" in
  coverage-prepare)
    ensure_coverage_cache
    echo "VCS coverage cache ready: $vcs_build_root/coverage"
    ;;
  build)
    require_rtl
    # Compile from a prepared copy: the prepare step wraps the firtool
    # non-synthesis blocks in VCS coverage pragmas (see the script), which
    # keeps code-coverage numbers meaningful for the plain build as well.
    bash "$project_root/vcs/scripts/prepare_coverage_rtl.sh" "$rtl_dir" "$vcs_build_root/test/rtl"
    compile_simv "$vcs_build_root/test" off "$vcs_build_root/test/rtl"
    ;;
  run)
    require_rtl
    # Plain simv is compiled once and reused (same reuse rule as coverage);
    # TLTEST_VCS_REBUILD=1 forces recompilation.
    if [[ ! -f "$vcs_build_root/test/simv" || "${TLTEST_VCS_REBUILD:-0}" == 1 ]]; then
      bash "$project_root/vcs/scripts/prepare_coverage_rtl.sh" "$rtl_dir" "$vcs_build_root/test/rtl"
      compile_simv "$vcs_build_root/test" off "$vcs_build_root/test/rtl"
    fi
    run_simv "$(cd "$vcs_build_root/test" && pwd)/simv" "" "" ""
    ;;
  coverage)
    require_rtl
    # Resolve which trace to replay: explicit path > traces/<name> (with
    # .ops/.trace/.txt extensions tried in turn) > whatever the ini
    # configures (labeled matrix_trace).
    trace_name=${2:-}
    trace_path=""
    if [[ -n "${TLTEST_VCS_TRACE:-}" ]]; then
      trace_path=$(realpath "$TLTEST_VCS_TRACE")
      trace_name=$(basename "$trace_path"); trace_name=${trace_name%.*}
    elif [[ -n "$trace_name" ]]; then
      for ext in "" .ops .trace .txt; do
        if [[ -f "$project_root/traces/$trace_name$ext" ]]; then
          trace_path=$(realpath "$project_root/traces/$trace_name$ext")
          trace_name="$trace_name$ext"; trace_name=${trace_name%.*}
          break
        fi
      done
      if [[ -z "$trace_path" ]]; then
        echo "error: trace file not found under $project_root/traces: $trace_name{.ops,.trace,.txt}" >&2
        exit 2
      fi
    else
      trace_name=matrix_trace
    fi
    echo "VCS coverage trace: $trace_name ${trace_path:+($trace_path)}"

    build_dir="$vcs_build_root/coverage"
    ensure_coverage_cache
    mkdir -p "$coverage_output_dir/vdb" "$coverage_output_dir/reports" "$coverage_output_dir/logs"
    coverage_vdb="$coverage_output_dir/vdb/$trace_name.vdb"
    rm -rf "$coverage_vdb"
    cp -r "$build_dir/template.vdb" "$coverage_vdb"
    run_simv "$(cd "$build_dir" && pwd)/simv" "$coverage_vdb" "$trace_path" "$trace_name"
    cp "${run_root}_$trace_name/simv.log" "$coverage_output_dir/logs/$trace_name.log"
    urg -dir "$coverage_vdb" -hier "$coverage_hier" -report "$coverage_output_dir/reports/$trace_name"
    echo "VCS coverage report: $coverage_output_dir/reports/$trace_name/dashboard.html"
    echo "Merge everything later with: $0 coverage-merge"
    ;;
  coverage-fuzz|coverage-case)
    # Both modes have explicit run labels; coverage-case also overrides the trace.
    trace_path=""
    if [[ "$mode" == coverage-case ]]; then
      if [[ $# -ne 4 || ! "$2" =~ ^[A-Za-z0-9][A-Za-z0-9_.-]*$ ]]; then
        echo "usage: $0 coverage-case <safe-name> <ini> <trace-path>" >&2
        exit 2
      fi
      run_name=$2
      export TLTEST_VCS_INI=$3
      if [[ ! -f "$4" ]]; then
        echo "error: trace file not found: $4" >&2
        exit 2
      fi
      trace_path=$(realpath "$4")
    else
      run_name=${2:-fuzz_ari}
      if [[ -z "${3:-}" && -z "${TLTEST_VCS_INI:-}" ]]; then
        echo "usage: $0 coverage-fuzz <name> <ini>   (e.g. a FUZZ_ARI config)" >&2
        exit 2
      fi
      export TLTEST_VCS_INI=${3:-${TLTEST_VCS_INI}}
    fi
    if [[ ! -f "$TLTEST_VCS_INI" ]]; then
      echo "error: ini not found: $TLTEST_VCS_INI" >&2
      exit 2
    fi
    require_rtl
    echo "VCS $mode: $run_name (ini: $TLTEST_VCS_INI)"

    build_dir="$vcs_build_root/coverage"
    ensure_coverage_cache
    mkdir -p "$coverage_output_dir/vdb" "$coverage_output_dir/reports" "$coverage_output_dir/logs"
    coverage_vdb="$coverage_output_dir/vdb/$run_name.vdb"
    rm -rf "$coverage_vdb"
    cp -r "$build_dir/template.vdb" "$coverage_vdb"
    run_simv "$(cd "$build_dir" && pwd)/simv" "$coverage_vdb" "$trace_path" "$run_name"
    cp "${run_root}_$run_name/simv.log" "$coverage_output_dir/logs/$run_name.log"
    urg -dir "$coverage_vdb" -hier "$coverage_hier" -report "$coverage_output_dir/reports/$run_name"
    echo "VCS coverage report: $coverage_output_dir/reports/$run_name/dashboard.html"
    echo "Merge everything later with: $0 coverage-merge"
    ;;
  coverage-merge)
    vdbs=("$coverage_output_dir"/vdb/*.vdb)
    if [[ ! -d "${vdbs[0]}" ]]; then
      echo "error: no vdbs under $coverage_output_dir/vdb; run 'coverage <trace>' first" >&2
      exit 2
    fi
    echo "Merging ${#vdbs[@]} vdbs: ${vdbs[*]}"
    urg -dir "${vdbs[@]}" -hier "$coverage_hier" -report "$coverage_output_dir/report_merged"
    echo "VCS merged coverage report: $coverage_output_dir/report_merged/dashboard.html"
    ;;
  *)
    echo "usage: $0 {build|run|coverage-prepare|coverage [trace]|coverage-fuzz [name] [ini]|coverage-case <name> <ini> <trace>|coverage-merge}" >&2
    exit 2
    ;;
esac
