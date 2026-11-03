# VCS trace 仿真

## 环境

```bash
module load license
module load synopsys/vcs/Q-2020.03-SP2
scl enable devtoolset-11 zsh
```

## 首次准备（只做一次）

```bash
make -C dut/XSCache test-top-matrix NUM_CORE=1 NUM_SLICE=1
make tltest-config-openLLC-test-matrix-trace
make tltest-build-dpi-openLLC
```

## 跑 trace

```bash
make vcs-run
```

首次自动编译 `build_vcs/test/simv`，之后复用（`TLTEST_VCS_REBUILD=1` 强制重编）。
输出同时打印到终端并写入 `run/vcs_run/simv.log`。成功标志：`tl-test subsystem FINISHED`。

## 换 trace

改 `trace.filepath`（`main/build/tltest.ini`，源文件在 `configs/`），路径相对 `run/vcs_run`。


## 覆盖率

```bash
make vcs-coverage TRACE=matrix-trace-64sets
make vcs-coverage-merge
```

`TRACE` 是 `traces/` 下的文件名（扩展名可省略），不是绝对路径。

报告在 `build_vcs/coverage/reports/`。
