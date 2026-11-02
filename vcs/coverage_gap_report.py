#!/usr/bin/env python3
"""Per-module coverage gap triage for a merged urg report.

The hierarchy page (hierarchy1.html) only carries percentages; the per-module
pages (mod*.html) carry the actual Total/Covered counts. This script joins
both:

  * hierarchy1.html  -> every DUT instance: flattened path, module page,
                        per-metric score (Score/Line/Cond/Toggle/FSM columns)
  * mod*.html        -> module name (page <title>) + Total/Covered counts for
                        Line (TOTAL row), Cond (Conditions row), Toggle
                        (Total Bits row) and FSM (Transitions rows)

Gap volume per instance = module total * (100 - instance %) / 100, which
reconciles with the session.xml totals urg reports for the whole scope.

Every module is classified into buckets (see CLASSIFY below):

  struct  结构性死区   — unreachable in the current testbench/DUT build
  config  需改配置     — reachable only after testbench/DUT config changes
  fuzz    随机模式可达 — plausibly hit by FUZZ_ARI/FUZZ_STREAM stimulus
  tb      插桩/外围    — testbench instrumentation, not DUT cache logic

Usage:
  coverage_gap_report.py [REPORT_DIR] [-o OUTPUT.md]
  REPORT_DIR defaults to build_vcs/coverage/report_merged (this directory's
  parent), OUTPUT.md defaults to <repo>/../../docs/coverage-gap-notes.md.
"""

import argparse
import re
import sys
from pathlib import Path

METRICS = ('line', 'cond', 'toggle', 'fsm')

BUCKET_TITLES = {
    'struct': '结构性死区（当前构建下不可达）',
    'config': '需改配置（改 testbench/DUT 参数或端口拓扑，需与上游讨论）',
    'fuzz':   '随机模式可达（FUZZ_ARI / FUZZ_STREAM 激励可打）',
    'tb':     '插桩/外围（testbench instrumentation，非 DUT 缓存逻辑）',
}

# Module -> (bucket, reason). Keys are the urg module names (page titles);
# modules not listed fall through to FALLBACK rules and then the fuzz default.
CLASSIFY = {
    # ---- 插桩/外围：TL 端口记录器、M 通道日志（M 通道在本构建中从不 fire）、
    #      外存模型。它们的 gap 与缓存逻辑无关，单列不计入三档。
    'TLLogger':        ('tb', '端口事务记录器：未 fire 的通道/操作码分支'),
    'TLLogger_1':      ('tb', '同 TLLogger'),
    'TLLogger_2':      ('tb', '同 TLLogger'),
    'TLLogger_3':      ('tb', '同 TLLogger'),
    'TLLogger_4':      ('tb', '同 TLLogger'),
    'TLLoggerM':       ('tb', 'M 通道记录器，本构建 M 通道从不 fire'),
    'TLLogWriter':     ('tb', 'ChiselDB 写出路径'),
    'L2MPWriter':      ('tb', 'L2 matrix 写出路径，依赖 M 通道'),
    'mem_ext':         ('tb', '外存行为模型'),
    'TestTop':         ('tb', '顶层组装模块，gap 为端口位翻转'),

    # ---- 结构性死区：当前拓扑下激励到达不了。
    'SinkB_1':         ('struct', 'l3.sinkB：L3 上游（内存侧）从不发 B 通道 probe'),

    # ---- 需改配置：路径存在但被参数/拓扑锁死。
    'ProbeHelper':     ('config', 'dir_conflict 需 clientDir 溢出；clientDir 768 条 '
                                  '>> L2 实际 256 块，永不冲突。需调 clientCaches 配置'),
    'Queue5_MSHRRequest': ('config', 'probeHelperOpt.queue：同 ProbeHelper，排队路径 '
                                     '只有 dir_conflict 才进'),
    'Arbiter2_MSHRRequest': ('config', 'l3.b_arb：未覆盖分支为 probeHelper 输入端口，'
                                       '随 ProbeHelper 解锁'),
    'SinkA_1':         ('config', 'l3.sinkA：未覆盖行主要是 CMO/自定义 hint 操作码；'
                                  'CFuzzer 的 CBO 只在 cmo 区间发，hint 无来源'),
    'CustomL1Hint':    ('config', 'L1 自定义 hint，当前无激励来源'),
    'L1HintArb':       ('config', '同 CustomL1Hint'),
    'Queue4_HintQueueEntry': ('config', 'L1 hint 队列，同上'),
    'SourceA':         ('config', 'l3.sourceA：未覆盖行含向内存发的 hint/prefetch 操作码，'
                                  '当前激励不生成'),

    # ---- 随机模式可达：明确点名的高价值目标。
    'SinkC':           ('fuzz', 'C 通道 probeAck/Release 参数变体（TIP/Branch/Trunk、'
                                'grow/shrink 组合），随机 grow/shrink 可枚举'),
    'SinkC_1':         ('fuzz', '同 SinkC（另一实例侧）'),
    'SourceB':         ('fuzz', 'probe 发射：hit/miss × 脏净 × TIP/Trunk/Branch 组合'),
    'SourceB_1':       ('fuzz', '同 SourceB'),
    'MSHR':            ('fuzz', 'MSHR 任务 FSM 的并发/参数分支组合'),
    'MSHR_16':         ('fuzz', '同 MSHR'),
    'Slice_1':         ('fuzz', '目录写状态机 FSM 迁移（N/S/M 态巡回）'),
    'RRArbiterInit':   ('fuzz', '仲裁轮转 FSM：迁移需持续竞争翻转 grant'),
    'GrantBuffer':     ('fuzz', 'grant 合并/拆分路径需特定 acquire 参数组合'),
    'DataStorage_1':   ('fuzz', 'bank 部分写/掩码条件需非整块数据'),
    'IDPool':          ('fuzz', 'id 分配/释放交错时序'),
    'MainPipe':        ('fuzz', '主管线 cond 残留为并发时序组合'),
    'Directory':       ('fuzz', '目录读写竞争时序'),
    'SourceD':         ('fuzz', '写回 putBuffer 路径需驱逐压力下的脏块流'),
    'Queue2_TLBundleB_1': ('fuzz', 'B 通道队列深层 toggle：需 probe 并发'),
    'SourceE':         ('fuzz', 'E 通道路径需高频 acquire 的 grant ack 流'),
}

# 正则兜底：按模块名模式归类（先精确表，再模式，最后 fuzz 默认）。
FALLBACK = [
    (re.compile(r'^TLLogWriter'), 'tb', 'ChiselDB 通道写出器，同 TLLogger 插桩'),
    (re.compile(r'^Queue\d+_Hint|Hint'), 'config', 'L1 hint 通路，无激励来源'),
    (re.compile(r'^MSHR|^Queue\d+_MSHR'), 'fuzz', 'MSHR 相关并发/参数分支'),
    (re.compile(r'Arbiter|^Fast|^Latch'), 'fuzz', '仲裁竞争翻转分支'),
    (re.compile(r'^array|SRAM|Splitted|^mem$'), 'fuzz', 'SRAM 行为模型小 residue'),
]

FUZZ_DEFAULT_REASON = '随机并发/参数分支组合'


def strip_tags(html):
    return re.sub(r'\s+', ' ', re.sub(r'<[^>]+>', ' ', html))


def parse_hierarchy(report_dir):
    """hierarchy1.html -> [(flattened_path, modno, {metric: pct})]"""
    txt = (report_dir / 'hierarchy1.html').read_text(
        encoding='utf-8', errors='replace')
    row_re = re.compile(
        r'<tr[^>]*data-tt-id="([^"]*)"(?:\s+data-tt-parent-id="([^"]*)")?[^>]*>'
        r'(.*?)</tr>', re.S)
    info = {}
    for m in row_re.finditer(txt):
        rid, parent, body = m.group(1), m.group(2), m.group(3)
        # modN.html 为模块页；modN_M.html 是按实例组拆分的同名模块页
        # （mshrs_N 各实例），totals 相同，仍归并到基础页 modN。
        a = re.search(r'<a href="mod(\d+)(?:_\w+)?\.html[^"]*"[^>]*>(.*?)</a>',
                      body, re.S)
        modno = a.group(1) if a else None
        name = strip_tags(a.group(2)).strip() if a else strip_tags(body).strip()
        # 数值列按位置解析：Score / Line / Cond / Toggle / FSM。
        # 空单元格（指标被排除出 score）不能跳过，否则错位。
        cells = re.findall(r'<td class="[^"]*cl rt">\s*([0-9.]*)\s*</td>', body)
        pcts = {}
        for metric, cell in zip(METRICS, cells[1:5]):
            if cell:
                pcts[metric] = float(cell)
        info[rid] = (parent, name.strip(), modno, pcts)

    def flatten(rid):
        parts = []
        while rid and rid in info:
            parts.append(info[rid][1])
            rid = info[rid][0]
        # 根节点名字为空，join 后去掉前导 '.'
        return '.'.join(reversed(parts)).lstrip('.')

    out = []
    for rid, (parent, name, modno, pcts) in info.items():
        if not modno or not pcts:
            continue  # 根/容器节点或无覆盖数据的行
        path = flatten(rid)
        if path:
            out.append((path, modno, pcts))
    return out


def parse_module(report_dir, modno):
    """mod*.html -> (name, {metric: (total, covered)})"""
    txt = (report_dir / f'mod{modno}.html').read_text(
        encoding='utf-8', errors='replace')
    title = re.search(r'<title>([^<]*)</title>', txt)
    name = title.group(1).split('::')[-1].strip() if title else f'mod{modno}'
    flat = strip_tags(txt)

    def grab(pattern):
        m = re.search(pattern, flat)
        return (int(m.group(1)), int(m.group(2))) if m else (0, 0)

    # Line/Cond 区段缺失 = 该指标被 urg 排除出 score（全未覆盖的模块常见）
    counts = {
        'line':   grab(r'Line Coverage for Module :.*?TOTAL (\d+) (\d+)'),
        'cond':   grab(r'Cond Coverage for Module :.*?Conditions (\d+) (\d+)'),
        'toggle': grab(r'Toggle Coverage for Module :.*?Total Bits (\d+) (\d+)'),
        'fsm':    (0, 0),
    }
    st_tot = st_cov = 0
    for m in re.finditer(
            r'Summary for FSM ::.*?States (\d+) (\d+).*?Transitions (\d+) (\d+)',
            flat):
        st_tot += int(m.group(3))
        st_cov += int(m.group(4))
    counts['fsm'] = (st_tot, st_cov)
    return name, counts


def classify(name):
    if name in CLASSIFY:
        return CLASSIFY[name]
    for rx, bucket, reason in FALLBACK:
        if rx.search(name):
            return bucket, reason
    return 'fuzz', FUZZ_DEFAULT_REASON


def main():
    default_report = Path(__file__).resolve().parent.parent / \
        'build_vcs/coverage/report_merged'
    ap = argparse.ArgumentParser()
    ap.add_argument('report_dir', nargs='?', default=default_report,
                    type=Path, help='urg 报告目录（含 hierarchy1.html/mod*.html）')
    ap.add_argument('-o', '--output', default=None, type=Path,
                    help='输出 markdown 路径')
    ap.add_argument('--top', type=int, default=40, help='每张表列出的条数')
    args = ap.parse_args()

    report_dir = args.report_dir.resolve()
    if not (report_dir / 'hierarchy1.html').exists():
        sys.exit(f'error: {report_dir}/hierarchy1.html not found')

    instances = parse_hierarchy(report_dir)
    modules = {}  # modno -> {'name', 'counts', 'insts': [(path, pcts, gaps)]}
    for path, modno, pcts in instances:
        if modno not in modules:
            name, counts = parse_module(report_dir, modno)
            modules[modno] = {'name': name, 'counts': counts, 'insts': []}
        counts = modules[modno]['counts']
        gaps = {m: round(counts[m][0] * (100 - pcts.get(m, 100.0)) / 100)
                for m in METRICS}
        modules[modno]['insts'].append((path, pcts, gaps))

    # hierarchy1.html 的树表是截断的（urg 每页行数上限）：顶层 logger 容器、
    # TestTop 等实例不在表里，但在 cm_hier 记分范围内（scope = tltest_vcs_tb.dut）。
    # 这些模块按 mod 页 union 缺口计入（单实例模块即精确值）。
    in_tree = {modno for _, modno, _ in instances}
    for page in sorted(report_dir.glob('mod[0-9]*.html')):
        if '_' in page.stem:
            continue  # modN_M.html 是按实例组拆分的同名页，totals 同基础页
        modno = re.search(r'\d+', page.name).group()
        if modno in in_tree:
            continue
        name, counts = parse_module(report_dir, modno)
        if not any(counts[m][0] for m in METRICS):
            continue
        gaps = {m: counts[m][0] - counts[m][1] for m in METRICS}
        modules[modno] = {
            'name': name, 'counts': counts, 'insts': [],
            'absent': gaps,
        }

    # 与 session.xml 对账
    sess = {}
    sx = report_dir / 'session.xml'
    if sx.exists():
        for m in re.finditer(
                r'<metric\s+name="(Line|Cond|Toggle|FSM)"[^>]*value="(\d+)/(\d+)"',
                sx.read_text(encoding='utf-8', errors='replace')):
            sess[m.group(1).lower()] = (int(m.group(3)), int(m.group(2)))
    lines = []
    say = lines.append
    say('# 覆盖缺口三档分类报告')
    say('')
    say(f'- 数据源：`{report_dir}`（urg 合并报告）')
    say('- 口径：按模块 mod 页的 Total/Covered 计数 × 层级页各实例百分比，'
        '得到每个实例的未覆盖量；与 session.xml 总分对账。')
    say('- 档位：结构性死区 / 需改配置 / 随机模式可达，另单列"插桩/外围"'
        '（TLLogger 等测试侧记录器，不属于 DUT 缓存逻辑）。')
    say('')
    say('## 总览')
    say('')
    say('| 指标 | session.xml | 按实例累计 | 差值 |')
    say('|---|---|---|---|')
    for metric, label in [('line', 'Line'), ('cond', 'Cond'),
                          ('toggle', 'Toggle'), ('fsm', 'FSM')]:
        tot = 0
        gap = 0
        for m in modules.values():
            if m['insts']:
                tot += m['counts'][metric][0] * len(m['insts'])
                gap += sum(inst[2][metric] for inst in m['insts'])
            elif 'absent' in m:  # 层级表截断，单实例按 union 计
                tot += m['counts'][metric][0]
                gap += m['absent'][metric]
        cov = tot - gap
        s = sess.get(metric)
        diff = ''
        if s:
            diff = f"{s[1]-cov:+d}/{s[0]-tot:+d}"
        say(f'| {label} | {s[1] if s else "?"}/{s[0] if s else "?"} '
            f'| {cov}/{tot} | {diff} |')
    say('')

    # 按模块聚合（实例求和；层级表外模块按 union）
    mods = []
    for modno, m in modules.items():
        bucket, reason = classify(m['name'])
        if m['insts']:
            agg = {metric: sum(inst[2][metric] for inst in m['insts'])
                   for metric in METRICS}
            worst = sorted(m['insts'], key=lambda i: -sum(i[2].values()))
            paths = [i[0] for i in worst[:3]]
            n_inst = len(m['insts'])
        else:
            agg = dict(m['absent'])
            paths = ['（层级表截断，按 union 缺口）']
            n_inst = 1
        mods.append({
            'name': m['name'], 'bucket': bucket, 'reason': reason,
            'n_inst': n_inst, 'agg': agg, 'paths': paths,
            'union': m['counts'],
        })

    def bucket_summary(metric):
        say(f'### {"Line" if metric=="line" else {"cond":"Cond","toggle":"Toggle","fsm":"FSM"}[metric]}'
            f' 未覆盖量按档汇总')
        say('')
        say('| 档 | 未覆盖量 | 占比 |')
        say('|---|---|---|')
        total = sum(m['agg'][metric] for m in mods)
        for bucket in ('struct', 'config', 'fuzz', 'tb'):
            v = sum(m['agg'][metric] for m in mods if m['bucket'] == bucket)
            pct = f'{100*v/total:.1f}%' if total else '-'
            say(f'| {BUCKET_TITLES[bucket]} | {v} | {pct} |')
        say('')

    say('## 未覆盖量按档汇总')
    say('')
    for metric in METRICS:
        bucket_summary(metric)

    def module_table(metric, top):
        label = {'line': 'Line', 'cond': 'Cond',
                 'toggle': 'Toggle', 'fsm': 'FSM'}[metric]
        say(f'## {label} 未覆盖量 Top{top}（按模块，实例累计）')
        say('')
        say('| 模块 | 实例数 | Line 缺 | Cond 缺 | Toggle 缺 | FSM 缺 | 档 | 理由 | 主要实例 |')
        say('|---|---|---|---|---|---|---|---|---|')
        for m in sorted(mods, key=lambda x: -x['agg'][metric])[:top]:
            if not any(m['agg'][k] for k in METRICS):
                continue
            cells = ' | '.join(str(m['agg'][k]) for k in METRICS)
            say(f"| {m['name']} | {m['n_inst']} | {cells} "
                f"| {m['bucket']} | {m['reason']} | {'<br>'.join(m['paths'])} |")
        say('')

    for metric in METRICS:
        module_table(metric, args.top)

    say('## FUZZ 验收目标模块清单')
    say('')
    say('FUZZ_ARI / FUZZ_STREAM 首轮的观察对象：fuzz 档模块按 Line+Cond '
        '未覆盖量排序（纯 toggle 小残留已略去），跑完后对照本表逐模块核对增量。')
    say('')
    say('| 模块 | Line 缺 | Cond 缺 | Toggle 缺 | FSM 缺 | 理由 |')
    say('|---|---|---|---|---|---|')
    for m in sorted((x for x in mods if x['bucket'] == 'fuzz'),
                    key=lambda x: -(x['agg']['line'] + x['agg']['cond'])):
        a = m['agg']
        if not ((a['line'] + a['cond']) or a['fsm'] or a['toggle'] >= 1000):
            continue
        cells = ' | '.join(str(a[k]) for k in METRICS)
        say(f"| {m['name']} | {cells} | {m['reason']} |")
    say('')

    # 结论：插桩占比 + 排除插桩后的等效口径
    say('## 结论')
    say('')
    tb_tot = {k: 0 for k in METRICS}
    tb_gap = {k: 0 for k in METRICS}
    for m in mods:
        if m['bucket'] != 'tb':
            continue
        mult = m['n_inst']
        for k in METRICS:
            tb_tot[k] += m['union'][k][0] * mult
            tb_gap[k] += m['agg'][k]
    label = {'line': 'Line', 'cond': 'Cond', 'toggle': 'Toggle', 'fsm': 'FSM'}
    for k in METRICS:
        total_gap = sum(m['agg'][k] for m in mods)
        if not total_gap or not tb_tot[k]:
            continue
        l3log = next((m['agg'][k] for m in mods if m['name'] == 'TLLogger_3'), 0)
        extra = (f'，单 TLLogger_3（L3 出口 256-bit 记录器，未 fire 通道/'
                 f'操作码的记录分支）一家占 {l3log}') if l3log else ''
        say(f'- **{label[k]}**：总缺口 {total_gap}，其中插桩 {tb_gap[k]}'
            f'（{100*tb_gap[k]/total_gap:.0f}%）{extra}。')
    say('')
    if sess:
        say('若把 TLLogger/TLLogWriter/mem_ext 等插桩从 `hbl2_coverage.hier` '
            '排除，等效口径（DUT 缓存逻辑）覆盖率为：')
        say('')
        say('| 指标 | 全口径（当前） | 排除插桩后 |')
        say('|---|---|---|')
        name_map = {'line': 'Line', 'cond': 'Cond',
                    'toggle': 'Toggle', 'fsm': 'FSM'}
        for k in METRICS:
            s = sess.get(k)
            if not s or not tb_tot[k]:
                continue
            t2, c2 = s[0] - tb_tot[k], s[1] - (tb_tot[k] - tb_gap[k])
            say(f'| {name_map[k]} | {100*s[1]/s[0]:.2f}% '
                f'({s[1]}/{s[0]}) | {100*c2/t2:.2f}% ({c2}/{t2}) |')
        say('')
        say('排除插桩属于报告口径调整，不影响仿真本身；是否改 '
            '`hbl2_coverage.hier` 建议与上游确认。')
    say('')

    say('## 档位说明')
    say('')
    say('- **结构性死区**：当前 testbench/DUT 构建下激励在拓扑上到不了'
        '（如 L3 上游不发 probe、M 通道未启用）。换激励方式无解，'
        '要么接受、要么改构建。')
    say('- **需改配置**：路径本身存在，但被参数锁死（clientDir 容量、'
        '端口通道、hint 激励来源）。涉及 testbench/DUT 配置，'
        '需先与上游（陈熙）讨论再动。')
    say('- **随机模式可达**：trace 回放表达不了（单发顺序、64B 整块）'
        '但随机激励可以：并发时序、参数组合、CMO、非整块数据等。'
        '这是 FUZZ 首轮的验收范围。')
    say('- **插桩/外围**：TLLogger/mem_ext 等测试侧组件，gap 不反映'
        '缓存逻辑质量，可考虑后续用 cm_hier 排除。')
    say('')

    text = '\n'.join(lines)
    out = args.output
    if out is None:
        out = Path(__file__).resolve().parent.parent.parent / 'docs' / \
            'coverage-gap-notes.md'
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(text, encoding='utf-8')
    print(f'wrote {out}')
    # 控制台摘要
    for metric in METRICS:
        total = sum(m['agg'][metric] for m in mods)
        parts = []
        for bucket in ('struct', 'config', 'fuzz', 'tb'):
            v = sum(m['agg'][metric] for m in mods if m['bucket'] == bucket)
            if v:
                parts.append(f'{bucket}={v}({100*v/total:.0f}%)' if total
                             else f'{bucket}={v}')
        print(f'{metric:6s} gap={total:8d}  ' + '  '.join(parts))


if __name__ == '__main__':
    main()
