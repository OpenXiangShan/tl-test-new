# VCS 覆盖率测试套件

套件文件列举每次运行的名称、trace 和 INI。执行器默认最多同时运行 8 项，按清单顺序汇总实际配置、背压参数及检查结果，最后合并本次通过项的 VDB。

## 一键运行

在 `tl-test-new` 目录执行：

```bash
python3 vcs/run_coverage_suite.py
```

并行度通过 `--jobs`（缩写 `-j`）设置，必须为正整数：

```bash
python3 vcs/run_coverage_suite.py             # 默认最多 8 项并发
python3 vcs/run_coverage_suite.py --jobs 4    # 最多 4 项并发
python3 vcs/run_coverage_suite.py -j 1        # 串行运行
```

该值限制同时执行的任务数量，各任务的仿真和报告阶段都包含在内。汇总中的 `parallelism` 保存实际配置；任务状态包含 pending、running、passed、failed，汇总行始终按清单排列。

不指定文件时默认读取 `vcs/suites/full.json`。默认通过现有 `vcs/run_vcs_remote.sh` 在 eda01 执行。沿用 `vcs/README_REMOTE.md` 中准备好的环境和 DPI 库，复用现有 VCS 编译缓存；首次缺少 simv 时自动编译。

也可以从远端入口发起整套任务：

```bash
vcs/run_vcs_remote.sh vcs-coverage-suite SUITE=vcs/suites/full.json
```

远端 make 入口通过 `SUITE_JOBS` 设置并行度，默认同样为 8：

```bash
vcs/run_vcs_remote.sh vcs-coverage-suite SUITE_JOBS=4
```

已初始化 EDA 环境的主机可使用 `--local`，直接调用本机 make。

预检清单、查看每条 trace 的实际背压参数：

```bash
python3 vcs/run_coverage_suite.py vcs/suites/full.json --dry-run
```

预检会一次性检查所有文件、运行名唯一性、INI 的 TRACE 开关与背压参数，不启动仿真。

## 套件清单

- `vcs/suites/full.json`：统一全集，**47 项**，包括 **43 项 TRACE、4 项 FUZZ**；其中 2 项启用 D.ready 背压，其余 45 项关闭。运行名按场景命名，清单按名称排序。
- `vcs/suites/smoke.json`：三项短测试，用于快速检查执行器和环境。

日常执行和增补任务统一维护 `full.json`。相同操作序列共用 trace，通过不同 INI 保留延迟、seed 和背压等场景差异；有效输入相同的任务只保留一项。4 项 FUZZ 保留各自的 seed、内存配置和 300000 的 maximum_cycle。全集只依赖仓库中的源输入。

配置可在不同 trace 间复用。例如，`configs/coverage/cache-trace.tltest.ini` 用于缓存与 Matrix 基线，`configs/tltest-mmio.tltest.ini` 用于 MMIO 读写基线。清单中的 `trace` 会覆盖 INI 的默认路径。单独运行时也应显式配对，例如：

```bash
vcs/run_vcs_remote.sh vcs-coverage-case NAME=mmio-write \
  INI=configs/tltest-mmio.tltest.ini TRACE_FILE=traces/mmio-write.ops
```

JSON 格式示例，所有 trace/INI 相对路径均以项目根目录为基准：

```json
{
  "version": 1,
  "name": "grant-check",
  "defaults": {
    "timeout_seconds": 300,
    "run_args": ["+vcd_start=0", "+vcd_stop=0"]
  },
  "jobs": [
    {
      "name": "hit-normal",
      "trace": "traces/directed3-hit-grant-observe.ops",
      "ini": "configs/directed3-base.tltest.ini"
    },
    {
      "name": "hit-stalled",
      "trace": "traces/directed3-hit-grant-observe.ops",
      "ini": "configs/directed3-d-backpressure.tltest.ini",
      "timeout_seconds": 600,
      "run_args": ["+vcd_start=0", "+vcd_stop=20000"]
    }
  ]
}
```

`name` 是运行标识。同一 trace 可以出现多次，只要运行名不同。每项可以覆盖默认的超时和 plusargs；`run_args` 按整组替换，不与默认参数拼接。

`mode` 默认是 `trace`。TRACE 项的 INI 必须启用 `[tltest.fuzzer] trace.enable = 1` 并包含 `trace.filepath`。执行器将清单中的 trace 绝对路径写入本次 INI 副本，原始输入保持原样。

FUZZ 项显式填写 `"mode": "fuzz"`，只指定 INI，不填写 trace；INI 中 trace.enable 必须关闭或省略。例如：

```json
{
  "name": "random-seed-1000",
  "mode": "fuzz",
  "ini": "configs/coverage/random-seed-1000.tltest.ini"
}
```

背压开关和 period/low/start 从每项 INI 读取并显示，不需要在套件文件重复填写。运行后的汇总再次读取保存的有效 INI，检查其与请求的配置一致。

套件 INI 的每个 section 和各 section 内的 key 必须唯一，重复定义会在预检时报告文件名和行号并退出 2。C++ 解析器跨重复 section 保留首段已有值，Python 默认会用后段覆盖；因此套件拒绝这种有歧义的输入。共享的 `cache-trace.tltest.ini` 已整理为唯一声明，`maximum_cycle=0` 保留其原先在 C++ 中生效的不设周期上限行为；模拟器仍受套件的秒级超时限制。

## 输出与覆盖率范围

默认每次创建一个新目录：

```text
build_vcs/suites/<套件名>/<UTC时间>-<唯一标识>/
  suite.json                         # 展开后的任务、输入文件 SHA256
  summary.json                       # 结构化结果、实际背压设置、覆盖率
  summary.md                         # 各项结果和合并覆盖率表
  launch/<运行名>.log                # 后端启动、仿真与 URG 输出
  launch/merge.log
  run/vcs_run_<运行名>/
    tltest.ini                       # 实际使用的配置
    simv.log
  coverage/vdb/<运行名>.vdb
  coverage/reports/<运行名>/          # 各项 HTML / session.xml
  coverage/quarantine/               # 失败或无效项的 VDB
  coverage/report_merged/             # 本套件通过项的合并报告
```

可以指定新输出目录：

```bash
python3 vcs/run_coverage_suite.py vcs/suites/full.json \
  --output build_vcs/suites/my-full-run
```

已有目录会被拒绝覆盖。执行中逐项原子更新汇总文件，保存已完成项的信息。套件输出与原有 `build_vcs/coverage/vdb`、历史报告和原运行日志隔离；编译缓存仍复用 `build_vcs/coverage/simv`。

**合并覆盖率包含本次套件通过的所有任务。** 全集包含基线、定向测试和背压回归，执行时从源输入重新仿真并合并，不依赖历史 VDB。汇总同时给出各项及合并后的 Line、Condition、Toggle 已覆盖数、总数与百分比。

## 完成检查与失败处理

每项必须满足：

- 后端正常退出、仿真达到 `tl-test subsystem FINISHED`，没有 Fatal/断言失败/数据不匹配。
- TRACE 项的读、写、逐出实际收发计数相等，pending_entries 为 0。FUZZ 项按现有 fuzzer 的 FINISHED 验收；原有 4 项在 maximum_cycle 后结束，不能把它表述为所有在途事务已排空。
- 输入 trace（TRACE 项）、有效 INI、SHA256 与清单对应；覆盖率报告包含全部三个指标，统计分母一致。

失败项会记录原因、隔离 VDB，并继续后续任务。模拟器超时在 EDA 主机上生效，先 TERM、10 秒后仍未退出则强制结束；外层另留 180 秒用于 SSH、编译和报告生成。每个本机 launcher 使用独立进程组；外层超时时先向该组发送 TERM，最多等待 10 秒，再 KILL 残留进程。即使 launcher 先退出，也会清理其进程组内的后代，完成后才释放该任务的并发名额。

合并只使用通过项，检查输入集合及覆盖率分母，并确认合并后的已覆盖数满足集合并集的上下界。汇总记录 simulator、DPI 库、TB 和 coverage hierarchy 的 SHA256，检查套件期间这些构建输入没有变化。

全部任务和合并通过时退出 0；存在失败任务或合并失败时退出 1；清单/路径等预检错误退出 2。没有通过项时跳过合并。

所有任务开始前先执行一次 `coverage-prepare`，复用或构建共享 simv/template 缓存；缓存准备使用文件锁，完成后才启动并行任务。每个任务使用独立的 INI、仿真日志、VDB 和报告目录，URG 也使用独立临时 launcher。汇总由主线程统一原子写入，等全部任务结束后才进行最终合并。

修改 RTL/TB 后，首次执行可设置 `TLTEST_VCS_REBUILD=1`；强制重编只发生在准备阶段，各 worker 禁止强制重编。准备失败时不启动任何仿真，保留 `launch/prepare.log` 并将套件标记失败。修改 C++ agent 后需先重建 DPI 库，和原有单项命令一致。

## 执行器测试

```bash
python3 -m unittest discover -s vcs/tests -p 'test_coverage_suite.py' -v
```

测试使用替身后端验证配置配对、失败隔离、超时与合并等行为，不占用 VCS。真实 VCS 套件结果位于各自输出目录的 `summary.md`。

执行器测试覆盖 TRACE/FUZZ 混合、配置配对、超时、失败隔离、并发限制、准备失败与合并校验等行为。套件预检和替身测试不能代替真实 VCS 仿真；覆盖率应以本次运行的报告为准。

## 两份早期源文件的恢复说明

原 `toggle-addr-sweep.ops`、`toggle-data-writes.ops` 已缺失，Git 也无对应历史；因此这两项使用明确标记的恢复版本：

- `toggle-data-writes`：依据原 simv.log 恢复实际发出的 192 条 MW/MR/LD 事务顺序，并增加最终 FE。原 256 项中的未发出操作和 fence 无法从日志完整恢复，不声称字节一致。
- `toggle-address-32bit`：恢复地址位扫描场景，显式等待读完成后逐出。地址范围限制为 32 位，移除旧日志中 0x100000000 的访问；该地址超出当前 AXI 地址宽度。对应内存配置设为 4GiB。

恢复文件位于 `traces/coverage/`，保留必要的来源与差异说明；这两项是恢复场景，不能视为原始 trace 的字节级复现。4 项 FUZZ 的内存参数各自保留，其中 `random-seed-1000-large-memory` 使用 8 GiB backing。
