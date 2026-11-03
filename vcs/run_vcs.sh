#!/usr/bin/env bash
# VCS backend for the XSAICache matrix trace replay (tl-test-new DPI Guest
# Mode). Expects the TestTop from 'make -C dut/XSCache test-top-matrix
# NUM_CORE=1 NUM_SLICE=1' (matches configs/coupledL2-test-matrix-trace).
#
# Usage:
#   vcs/run_vcs.sh build                  compile simv (no coverage instrumentation)
#   vcs/run_vcs.sh run                    compile if needed, replay the trace in
#                                         main/build/tltest.ini (the make-generated one)
#   vcs/run_vcs.sh coverage [trace]       coverage run of one trace -> own vdb + report
#                                         trace: a file under traces/ (extension optional,
#                                         .ops/.trace/.txt tried in turn); TLTEST_VCS_TRACE=<path>
#                                         overrides. Without an argument the trace configured
#                                         in the ini is replayed and labeled matrix_trace.
#                                         simv is compiled once and reused;
#                                         TLTEST_VCS_REBUILD=1 forces recompilation.
#   vcs/run_vcs.sh coverage-fuzz <name> <ini>
#                                         random-stimulus coverage run (no trace); the ini
#                                         (e.g. a FUZZ_ARI config) drives everything, name
#                                         labels the vdb/report
#   vcs/run_vcs.sh coverage-merge         urg-merge every vdb under build_vcs/coverage/vdb
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

project_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
rtl_dir_default="$project_root/dut/XSCache/build/openllc"
rtl_dir=${TLTEST_RTL_DIR:-$rtl_dir_default}
# generated C++ companions (chisel_db.cpp / perfCCT.cpp) live one level above
# the split-verilog directory
db_dir=$(cd "$(dirname "$rtl_dir")" && pwd)
vcs_build_root=${TLTEST_VCS_BUILD_DIR:-$project_root/build_vcs}
run_root="$project_root/run/vcs_run"
tb_file="$project_root/vcs/vcs_tb.sv"
coverage_hier="$project_root/vcs/coverage.hier"
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
urg() {
  mkdir -p "$vcs_build_root/tool"
  sed -e '1s|^#!.*$|#!/usr/bin/env bash|' \
      -e 's|\${VCS_HOME}/bin/vcs -location|bash ${VCS_HOME}/bin/vcs -location|g' \
      -e 's|ERR_MSG_REPORT=${BASE_STRING}/bin/vcsMsgReport|ERR_MSG_REPORT="bash ${BASE_STRING}/bin/vcsMsgReport"|' \
      "$VCS_HOME/bin/urg" > "$vcs_build_root/tool/urg"
  chmod +x "$vcs_build_root/tool/urg"
  bash "$vcs_build_root/tool/urg" -full64 "$@"
}

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
  rm -rf "$this_run_root"
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
    sed -i -E "s#^[[:space:]]*trace.filepath[[:space:]]*=.*#trace.filepath              = $trace_file#" \
      "$this_run_root/tltest.ini"
  fi
  printf '#!SV_LIBRARIES\n%s\n' "${dpi_lib%.so}" > "$this_run_root/bootstrap_lib_files"

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

  echo "VCS run in $this_run_root (log: $log_file)"
  (cd "$this_run_root" && "$simv" "${run_args[@]}") > "$log_file" 2>&1 || {
    tail -n 80 "$log_file" >&2
    echo "error: VCS exited unsuccessfully; see $log_file" >&2
    exit 1
  }

  # This VCS version reports $fatal with a zero process exit status, so the
  # simulator log is the authoritative result.
  if grep -q 'Fatal:' "$log_file"; then
    tail -n 80 "$log_file" >&2
    echo "error: VCS test failed; see $log_file" >&2
    exit 1
  fi
  if ! grep -q 'FINISHED' "$log_file"; then
    tail -n 80 "$log_file" >&2
    echo "error: replay did not reach FINISHED; see $log_file" >&2
    exit 1
  fi
  grep -m1 'FINISHED' "$log_file" || true
}

case "$mode" in
  build)
    require_rtl
    # Compile from a prepared copy: the prepare step wraps the firtool
    # non-synthesis blocks in VCS coverage pragmas (see the script), which
    # keeps code-coverage numbers meaningful for the plain build as well.
    bash "$project_root/vcs/prepare_coverage_rtl.sh" "$rtl_dir" "$vcs_build_root/test/rtl"
    compile_simv "$vcs_build_root/test" off "$vcs_build_root/test/rtl"
    ;;
  run)
    require_rtl
    # Plain simv is compiled once and reused (same reuse rule as coverage);
    # TLTEST_VCS_REBUILD=1 forces recompilation.
    if [[ ! -f "$vcs_build_root/test/simv" || "${TLTEST_VCS_REBUILD:-0}" == 1 ]]; then
      bash "$project_root/vcs/prepare_coverage_rtl.sh" "$rtl_dir" "$vcs_build_root/test/rtl"
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
    coverage_rtl_dir="$build_dir/rtl"
    mkdir -p "$build_dir/vdb" "$build_dir/reports" "$build_dir/logs"
    # The coverage simv is compiled once and reused across traces; only a
    # missing simv (or TLTEST_VCS_REBUILD=1) redoes prepare + compile, so the
    # vdbs of earlier traces survive.
    if [[ ! -f "$build_dir/simv" || "${TLTEST_VCS_REBUILD:-0}" == 1 ]]; then
      bash "$project_root/vcs/prepare_coverage_rtl.sh" "$rtl_dir" "$coverage_rtl_dir"
      compile_simv "$build_dir" on "$coverage_rtl_dir"
    fi
    coverage_vdb="$build_dir/vdb/$trace_name.vdb"
    rm -rf "$coverage_vdb"
    cp -r "$build_dir/template.vdb" "$coverage_vdb"
    run_simv "$(cd "$build_dir" && pwd)/simv" "$coverage_vdb" "$trace_path" "$trace_name"
    cp "$project_root/run/vcs_run_$trace_name/simv.log" "$build_dir/logs/$trace_name.log"
    urg -dir "$coverage_vdb" -hier "$coverage_hier" -report "$build_dir/reports/$trace_name"
    echo "VCS coverage report: $build_dir/reports/$trace_name/dashboard.html"
    echo "Merge everything later with: $0 coverage-merge"
    ;;
  coverage-fuzz)
    # Random-stimulus coverage run (no trace file): the ini drives everything.
    #   vcs/run_vcs.sh coverage-fuzz <name> <ini>
    # <name> labels the vdb/report (include the seed, e.g. fuzz_ari_s1000).
    require_rtl
    fuzz_name=${2:-fuzz_ari}
    if [[ -z "${3:-}" && -z "${TLTEST_VCS_INI:-}" ]]; then
      echo "usage: $0 coverage-fuzz <name> <ini>   (e.g. a FUZZ_ARI config)" >&2
      exit 2
    fi
    export TLTEST_VCS_INI=${3:-${TLTEST_VCS_INI}}
    if [[ ! -f "$TLTEST_VCS_INI" ]]; then
      echo "error: fuzz ini not found: $TLTEST_VCS_INI" >&2
      exit 2
    fi
    echo "VCS coverage fuzz: $fuzz_name (ini: $TLTEST_VCS_INI)"

    build_dir="$vcs_build_root/coverage"
    coverage_rtl_dir="$build_dir/rtl"
    mkdir -p "$build_dir/vdb" "$build_dir/reports" "$build_dir/logs"
    if [[ ! -f "$build_dir/simv" || "${TLTEST_VCS_REBUILD:-0}" == 1 ]]; then
      bash "$project_root/vcs/prepare_coverage_rtl.sh" "$rtl_dir" "$coverage_rtl_dir"
      compile_simv "$build_dir" on "$coverage_rtl_dir"
    fi
    coverage_vdb="$build_dir/vdb/$fuzz_name.vdb"
    rm -rf "$coverage_vdb"
    cp -r "$build_dir/template.vdb" "$coverage_vdb"
    run_simv "$(cd "$build_dir" && pwd)/simv" "$coverage_vdb" "" "$fuzz_name"
    cp "$project_root/run/vcs_run_$fuzz_name/simv.log" "$build_dir/logs/$fuzz_name.log"
    urg -dir "$coverage_vdb" -hier "$coverage_hier" -report "$build_dir/reports/$fuzz_name"
    echo "VCS coverage report: $build_dir/reports/$fuzz_name/dashboard.html"
    echo "Merge everything later with: $0 coverage-merge"
    ;;
  coverage-merge)
    build_dir="$vcs_build_root/coverage"
    vdbs=("$build_dir"/vdb/*.vdb)
    if [[ ! -d "${vdbs[0]}" ]]; then
      echo "error: no vdbs under $build_dir/vdb; run 'coverage <trace>' first" >&2
      exit 2
    fi
    echo "Merging ${#vdbs[@]} vdbs: ${vdbs[*]}"
    urg -dir "${vdbs[@]}" -hier "$coverage_hier" -report "$build_dir/report_merged"
    echo "VCS merged coverage report: $build_dir/report_merged/dashboard.html"
    ;;
  *)
    echo "usage: $0 {build|run|coverage [trace]|coverage-fuzz [name] [ini]|coverage-merge}" >&2
    exit 2
    ;;
esac
