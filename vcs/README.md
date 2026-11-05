# VCS trace 仿真

## 目录结构

- `scripts/`：VCS 本地与远程运行、套件调度、覆盖率 RTL 预处理和报告分析工具。
- `tb/`：SystemVerilog 仿真顶层。
- `config/`：覆盖率采集范围配置。
- `suites/`：覆盖率测试套件 JSON 清单。
- `tests/`：套件调度集成测试。
- `docs/`：[测试套件说明](docs/COVERAGE_SUITES.md)。

远程运行见 [README_REMOTE.md](README_REMOTE.md)。

以下命令均在项目根目录执行；`make vcs-*` 入口保持不变。

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

## 批量覆盖率套件

清单按运行名列出每项 trace 与 INI，一条命令执行并汇总：

```bash
python3 vcs/scripts/run_coverage_suite.py vcs/suites/full.json
```

脚本默认最多 8 项并发，可用 `--jobs 4`（或 `-j 4`）调整，`-j 1` 串行运行。
脚本自动走 eda01；输出各项及合并的 Line/Condition/Toggle，显式记录背压配置，并隔离失败 VDB。
支持同一 trace 搭配不同 INI。合并范围仅为本次套件的通过项。
清单格式、短测试与输出结构见 [COVERAGE_SUITES.md](docs/COVERAGE_SUITES.md)。
