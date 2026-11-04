# VCS trace 仿真（远程）

在其他主机通过 ssh 驱动 eda01，所有命令统一走 `vcs/scripts/run_vcs_remote.sh <make 目标>`。

## 前提

`ssh eda01` 免密可用，且两边共用同一份仓库（NFS）。主机名可用 `TLTEST_EDA_HOST` 覆盖。

## 首次准备（只做一次）

```bash
vcs/scripts/run_vcs_remote.sh vcs-setup
```

在 eda01 上生成 RTL、`main/build/tltest.ini` 和 DPI 库，并清掉旧的 `build_vcs`。

## 跑 trace

```bash
vcs/scripts/run_vcs_remote.sh vcs-run
```

输出同时打印到终端并写入 `run/vcs_run/simv.log`。成功标志：`tl-test subsystem FINISHED`。

## 覆盖率

```bash
vcs/scripts/run_vcs_remote.sh vcs-coverage TRACE=matrix-trace-64sets
vcs/scripts/run_vcs_remote.sh vcs-coverage-merge
```

报告在 `build_vcs/coverage/reports/`。

## 说明

- 任何 make 目标都可这样跑：`vcs/scripts/run_vcs_remote.sh <目标> [VAR=value ...]`
- `TLTEST_*` 变量自动转发；退出码原样返回
- 不要在本地编译，产物 ABI 与 eda01 不兼容
- 换 trace 见 [README.md](README.md)（它的「环境」一节只在 eda01 本机需要）

## 批量执行 trace / INI 清单

```bash
python3 vcs/scripts/run_coverage_suite.py vcs/suites/full.json
# 或者通过远端 make 入口启动整个套件：
vcs/scripts/run_vcs_remote.sh vcs-coverage-suite SUITE=vcs/suites/full.json
```

每项显式选择自己的 trace 和 INI；日志、背压参数、失败原因和覆盖率汇总保存在独立的
`build_vcs/suites/` 目录。详见 [测试套件说明](docs/COVERAGE_SUITES.md)。

套件默认 8 路并行。Python 入口使用 `--jobs N` / `-j N`；远端 make 入口使用：

```bash
vcs/scripts/run_vcs_remote.sh vcs-coverage-suite SUITE_JOBS=4
```
