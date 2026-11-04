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

## 可控 TL-C D 背压

默认 D.ready 始终为 1。定向测试可在 INI 的 `[tltest.config]` 中配置：

```ini
cagent.d_ready.period = 256
cagent.d_ready.low = 192
cagent.d_ready.start = 0
```

单位是 CAgent 的 clock tick（每次 `handle_channel()` 加 1；当前 VCS 每个硬件周期一次）。
`start` 之前保持 ready；之后每个 `period` 的前 `low` 拍拉低 D.ready，其余拍放行。
`low=0` 为关闭，允许 `period=0`；启用时要求 `0 < low < period`。
该选项作用于 TL-C agent，Matrix、MMIO 的 ready 和 E 通道保持各自原来的驱动方式。

VCS 在正沿保存 TL-C D 响应，负沿将该快照送给 C++，使背压切换时的计数对应实际握手。
首次使用这次框架更新，需要在 eda01 重建 DPI 库与 VCS 可执行文件；现有构建目录可用：

```bash
vcs/run_vcs_remote.sh -C main/build tltest_dpi -j8
TLTEST_VCS_REBUILD=1 \
TLTEST_VCS_INI="$PWD/configs/directed3-d-backpressure.tltest.ini" \
vcs/run_vcs_remote.sh vcs-coverage TRACE=directed3-hit-grant-backpressure
```

后续运行去掉 `TLTEST_VCS_REBUILD=1` 即可复用。

复现覆盖率时，应同时指定 trace 和该次测试的 INI。`TRACE` 不会自动加载背压配置。
以下两条示例启用背压：

| trace | INI | period / low / start（拍） |
|---|---|---|
| `directed3-hit-grant-backpressure` | `configs/directed3-d-backpressure.tltest.ini` | 256 / 192 / 0 |
| `directed3-grant-probe-backpressure` | `configs/directed3-d-backpressure-probe.tltest.ini` | 257 / 193 / 23 |
