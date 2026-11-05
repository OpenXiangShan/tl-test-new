#!/usr/bin/env python3
"""Run a JSON list of trace or fuzz jobs and merge only passing coverage databases."""
import argparse
import configparser
from concurrent.futures import FIRST_COMPLETED, ThreadPoolExecutor, wait
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import signal
import subprocess
import sys
import time
import uuid
import xml.etree.ElementTree as ET

PROJECT_ROOT = Path(__file__).resolve().parents[2]
IDENTIFIER = re.compile(r'[A-Za-z0-9][A-Za-z0-9_.-]*\Z')
FAILURE = re.compile(r'Fatal:|Assertion failed|assertion failure|Data mismatch|tl-test-new-FATAL|\bFAILED\b', re.I)
METRICS = ('Line', 'Cond', 'Toggle')
TERMINATE_GRACE_SECONDS = 10


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def read_ini(path):
    # Repeated sections merge differently in inicpp.hpp and ConfigParser.
    # Reject duplicates instead of validating a value the simulator may ignore.
    parser = configparser.ConfigParser(interpolation=None, strict=True)
    parser.optionxform = str  # Match the case-sensitive C++ INI keys.
    with Path(path).open() as stream:
        parser.read_file(stream)
    return parser


def backpressure(parser):
    values = {key: int(parser.get('tltest.config', 'cagent.d_ready.' + key, fallback='0'), 0)
              for key in ('period', 'low', 'start')}
    if min(values.values()) < 0 or (values['low'] and values['low'] >= values['period']):
        raise ValueError('D.ready settings require nonnegative values and low < period when enabled')
    return dict(enabled=bool(values['low']), period_ticks=values['period'],
                low_ticks=values['low'], start_ticks=values['start'])


def load_suite(path, root=None):
    """Validate every job before executing or creating any output directory."""
    root = Path(root or PROJECT_ROOT).resolve()
    path = Path(path).resolve()
    data = json.loads(path.read_text())
    if not isinstance(data, dict) or set(data) - {'version', 'name', 'description', 'defaults', 'jobs'}:
        raise ValueError('suite must be an object with version, name, defaults and jobs')
    if type(data.get('version')) is not int or data['version'] != 1:
        raise ValueError('suite version must be 1')
    if not isinstance(data.get('name'), str) or not IDENTIFIER.fullmatch(data['name']):
        raise ValueError('suite name must be a safe identifier')
    defaults = data.get('defaults', {})
    if not isinstance(defaults, dict) or set(defaults) - {'timeout_seconds', 'run_args'}:
        raise ValueError('defaults supports only timeout_seconds and run_args')
    if not isinstance(data.get('jobs'), list) or not data['jobs']:
        raise ValueError('suite jobs must be a nonempty list')
    names, jobs = set(), []
    for entry in data['jobs']:
        if not isinstance(entry, dict) or set(entry) - {'name', 'mode', 'trace', 'ini', 'timeout_seconds', 'run_args'}:
            raise ValueError('each job supports name, mode, trace, ini, timeout_seconds and run_args')
        name = entry.get('name')
        if not isinstance(name, str) or not IDENTIFIER.fullmatch(name) or name in names:
            raise ValueError('job names must be unique safe identifiers: ' + str(name))
        names.add(name)
        mode = entry.get('mode', 'trace')
        if mode not in ('trace', 'fuzz'):
            raise ValueError(name + ': mode must be trace or fuzz')
        if mode == 'fuzz' and 'trace' in entry:
            raise ValueError(name + ': fuzz jobs must not specify trace')
        job = dict(name=name, mode=mode)
        for key in (('trace', 'ini') if mode == 'trace' else ('ini',)):
            if not isinstance(entry.get(key), str) or not entry[key]:
                raise ValueError(name + ': missing ' + key)
            target = (root / entry[key]).resolve()
            if not target.is_file():
                raise ValueError(name + ': file not found: ' + str(target))
            job[key] = str(target)
            job[key + '_sha256'] = sha256(target)
        job['timeout_seconds'] = entry.get('timeout_seconds', defaults.get('timeout_seconds', 300))
        if type(job['timeout_seconds']) is not int or job['timeout_seconds'] <= 0:
            raise ValueError(name + ': timeout_seconds must be a positive integer')
        job['run_args'] = entry.get('run_args', defaults.get('run_args', ['+vcd_start=0', '+vcd_stop=0']))
        if not isinstance(job['run_args'], list) or any(
                not isinstance(arg, str) or not arg.startswith('+') or any(c.isspace() for c in arg)
                for arg in job['run_args']):
            raise ValueError(name + ': run_args must be a list of individual plusargs without whitespace')
        parser = read_ini(job['ini'])
        trace_enabled = int(parser.get('tltest.fuzzer', 'trace.enable', fallback='0').strip(), 0)
        if mode == 'trace':
            if trace_enabled != 1:
                raise ValueError(name + ': INI must set [tltest.fuzzer] trace.enable = 1')
            if not parser.has_option('tltest.fuzzer', 'trace.filepath'):
                raise ValueError(name + ': INI must include trace.filepath for the backend override')
        elif trace_enabled:
            raise ValueError(name + ': fuzz INI must disable trace.enable')
        job['d_ready_backpressure'] = backpressure(parser)
        jobs.append(job)
    return dict(version=1, name=data['name'], description=data.get('description', ''),
                manifest=str(path), manifest_sha256=sha256(path), jobs=jobs)


def read_coverage(report):
    tree = ET.parse(Path(report) / 'session.xml')
    scope = tree.find('.//scope[@type="top"]')
    if scope is None:
        raise ValueError('coverage report has no top scope')
    found = {}
    for metric in scope.findall('metric'):
        if metric.get('name') in METRICS:
            covered, total = map(int, metric.get('value', '').split('/'))
            if not 0 <= covered <= total:
                raise ValueError('invalid coverage counts')
            found[metric.get('name')] = dict(covered=covered, total=total,
                                           percent=100.0 * covered / total if total else None)
    if set(found) != set(METRICS):
        raise ValueError('coverage report must contain Line, Cond and Toggle')
    return found


def run_command(command, env, log_path, timeout):
    with Path(log_path).open('w') as log:
        # Each concurrent launcher owns its session, so cleanup cannot signal
        # another job or the suite coordinator. Its descendants inherit the group.
        with subprocess.Popen(command, cwd=str(PROJECT_ROOT), env=env,
                              stdout=log, stderr=subprocess.STDOUT,
                              start_new_session=True) as process:
            try:
                returncode = process.wait(timeout=timeout)
                return dict(returncode=returncode, timed_out=returncode == 124)
            except subprocess.TimeoutExpired:
                try:
                    os.killpg(process.pid, signal.SIGTERM)
                except ProcessLookupError:
                    pass
                try:
                    process.wait(timeout=TERMINATE_GRACE_SECONDS)
                except subprocess.TimeoutExpired:
                    pass
                finally:
                    # The launcher may exit on TERM while descendants ignore it.
                    # Always clear the remaining group, even after reaping its leader.
                    try:
                        os.killpg(process.pid, signal.SIGKILL)
                    except ProcessLookupError:
                        pass
                    process.wait()
                return dict(returncode=None, timed_out=True)


def command_for(target, local, **variables):
    prefix = ['make', '-C', str(PROJECT_ROOT)] if local else [str(PROJECT_ROOT / 'vcs/scripts/run_vcs_remote.sh')]
    return prefix + [target] + [key + '=' + str(value) for key, value in variables.items()]


def check_run(job, run_dir, vdb, report):
    log = run_dir / 'simv.log'
    text = log.read_text(errors='replace')
    errors = [line for line in text.splitlines() if FAILURE.search(line)]
    if errors:
        raise ValueError('simulator failure: ' + errors[0][:300])
    if not re.search(r'\btl-test subsystem FINISHED\b', text):
        raise ValueError('simulator did not reach FINISHED')
    effective = run_dir / 'tltest.ini'
    parser = read_ini(effective)
    original = read_ini(job['ini'])
    completion = {}
    if job['mode'] == 'trace':
        counts = re.findall(r'Trace summary: sent\{R/W/E\}=(\d+)/(\d+)/(\d+), recv\{R/W/E\}=(\d+)/(\d+)/(\d+), pending_entries=(\d+)', text)
        if not counts or counts[-1][:3] != counts[-1][3:6] or int(counts[-1][6]) != 0:
            raise ValueError('trace completion counters are missing or not drained')
        configured = Path(parser.get('tltest.fuzzer', 'trace.filepath'))
        if (run_dir / configured).resolve() != Path(job['trace']):
            raise ValueError('effective INI selected a different trace')
        original.set('tltest.fuzzer', 'trace.filepath', parser.get('tltest.fuzzer', 'trace.filepath'))
        completion = dict(completion_policy='trace_drained', sent_RWE=list(map(int, counts[-1][:3])),
                          received_RWE=list(map(int, counts[-1][3:6])))
    else:
        completion = dict(completion_policy='fuzzer_finished',
                          maximum_cycle=int(parser.get('tltest.config', 'maximum_cycle', fallback='0'), 0))
    if dict(original) != dict(parser):
        raise ValueError('effective INI differs from requested configuration')
    if not vdb.is_dir():
        raise ValueError('coverage database is missing')
    return dict(coverage=read_coverage(report), **completion,
                d_ready_backpressure=backpressure(parser), effective_ini=str(effective),
                effective_ini_sha256=sha256(effective), simv_log=str(log), simv_log_sha256=sha256(log))


def format_metric(value):
    if not value:
        return '-'
    percentage = '{:.2f}%'.format(value['percent']) if value['percent'] is not None else 'n/a'
    return '{} ({}/{})'.format(percentage, value['covered'], value['total'])


def atomic_write(path, text):
    temporary = path.with_name(path.name + '.tmp')
    temporary.write_text(text)
    temporary.replace(path)


def build_fingerprints(env):
    cache = Path(env.get('TLTEST_VCS_BUILD_DIR', str(PROJECT_ROOT / 'build_vcs')))
    if not cache.is_absolute():
        cache = PROJECT_ROOT / cache
    paths = [cache / 'coverage/simv', PROJECT_ROOT / 'main/build/libtltest_dpi.so',
             PROJECT_ROOT / 'vcs/tb/vcs_tb.sv', PROJECT_ROOT / 'vcs/config/coverage.hier']
    return {str(path): sha256(path) for path in paths if path.is_file()}


def write_summary(output, summary):
    atomic_write(output / 'summary.json', json.dumps(summary, ensure_ascii=False, indent=2) + '\n')
    rows = ['# Coverage suite: ' + summary['suite'], '',
            'Status: **{}**. Only passing jobs in this invocation are merged.'.format(summary['status']), '',
            'Parallel jobs: {}. Preparation: {}.'.format(summary['parallelism'], summary['preparation']['status']), '',
            '| Job | Mode | Status | INI | D.ready period/low/start (ticks) | Line | Condition | Toggle |',
            '|---|---|---|---|---|---|---|---|']
    for job in summary['jobs']:
        bp = job['d_ready_backpressure']
        settings = '{}/{}/{}'.format(bp['period_ticks'], bp['low_ticks'], bp['start_ticks']) if bp['enabled'] else 'off'
        metrics = job.get('coverage', {})
        rows.append('| {} | {} | {} | {} | {} | {} | {} | {} |'.format(
            job['name'], job['mode'], job['status'], job['ini'], settings,
            *(format_metric(metrics.get(key)) for key in METRICS)))
    rows += ['', '## Merged coverage', '']
    for key in METRICS:
        rows.append('- {}: {}'.format(key, format_metric((summary.get('coverage') or {}).get(key))))
    rows += ['', 'Merge: ' + summary['merge']['status'], '',
             'HTML report: `coverage/report_merged/dashboard.html`', '', '## Failures', '']
    rows.extend('- {}: {}'.format(job['name'], job['error']) for job in summary['jobs'] if job.get('error'))
    if summary['preparation'].get('error'):
        rows.append('- Preparation: ' + summary['preparation']['error'])
    if summary['merge'].get('error'):
        rows.append('- Merge: ' + summary['merge']['error'])
    atomic_write(output / 'summary.md', '\n'.join(rows) + '\n')


def fail_job(job, error, coverage_dir):
    job.update(status='failed', error=str(error))
    vdb = coverage_dir / 'vdb' / (job['name'] + '.vdb')
    if vdb.exists():
        quarantine = coverage_dir / 'quarantine' / vdb.name
        quarantine.parent.mkdir(parents=True, exist_ok=True)
        shutil.move(str(vdb), str(quarantine))
        job['quarantined_vdb'] = str(quarantine)


def run_job(spec, base_env, coverage_dir, run_root, output, local):
    """Workers own only their job files; the coordinator owns all suite summaries."""
    job = dict(spec, status='running', error=None)
    env = base_env.copy()
    env.update(TLTEST_VCS_RUN_ARGS=' '.join(job['run_args']), TLTEST_VCS_TIMEOUT=str(job['timeout_seconds']))
    vdb = coverage_dir / 'vdb' / (job['name'] + '.vdb')
    report = coverage_dir / 'reports' / job['name']
    run_dir = Path(str(run_root) + '_' + job['name'])
    launch_log = output / 'launch' / (job['name'] + '.log')
    if job['mode'] == 'trace':
        command = command_for('vcs-coverage-case', local, NAME=job['name'], INI=job['ini'], TRACE_FILE=job['trace'])
    else:
        command = command_for('vcs-coverage-fuzz', local, NAME=job['name'], INI=job['ini'])
    job.update(command=command, launch_log=str(launch_log), vdb=str(vdb), report=str(report))
    start = time.monotonic()
    job['started_at'] = datetime.now(timezone.utc).isoformat()
    try:
        result = run_command(command, env, launch_log, job['timeout_seconds'] + 180)
        if result['returncode'] and 'VCS simulation timed out' in launch_log.read_text(errors='replace'):
            result['timed_out'] = True
        job.update(result)
        if result['timed_out']:
            raise ValueError('test timed out')
        if result['returncode'] != 0:
            raise ValueError('launcher exited with status {}; see {}'.format(result['returncode'], launch_log))
        inputs = ('trace', 'ini') if job['mode'] == 'trace' else ('ini',)
        if any(sha256(job[key]) != job[key + '_sha256'] for key in inputs):
            raise ValueError('trace or INI changed during execution')
        job.update(check_run(job, run_dir, vdb, report))
        job['build_sha256'] = build_fingerprints(env)
        job['status'] = 'passed'
    except (ValueError, OSError, configparser.Error, ET.ParseError) as exc:
        fail_job(job, exc, coverage_dir)
    job['elapsed_seconds'] = round(time.monotonic() - start, 3)
    job['finished_at'] = datetime.now(timezone.utc).isoformat()
    return job


def run_suite(suite, output, local=False, parallelism=8):
    if type(parallelism) is not int or parallelism <= 0:
        raise ValueError('parallelism must be a positive integer')
    output = Path(output).resolve()
    output.mkdir(parents=True, exist_ok=False)
    (output / 'launch').mkdir()
    (output / 'suite.json').write_text(json.dumps(suite, ensure_ascii=False, indent=2) + '\n')
    coverage_dir = output / 'coverage'
    run_root = output / 'run/vcs_run'
    base_env = os.environ.copy()
    for key in ('TLTEST_VCS_TRACE', 'TLTEST_VCS_INI', 'TLTEST_VCS_RUN_ARGS', 'TLTEST_VCS_TIMEOUT'):
        base_env.pop(key, None)
    base_env.update(TLTEST_VCS_COVERAGE_DIR=str(coverage_dir), TLTEST_VCS_RUN_ROOT=str(run_root))
    summary = dict(suite=suite['name'], status='running', output=str(output), parallelism=parallelism,
                   jobs=[dict(spec, status='pending', error=None) for spec in suite['jobs']],
                   coverage=None, preparation=dict(status='running'), merge=dict(status='pending'))
    print('Suite output: ' + str(output), flush=True)
    print('Parallel jobs: {}'.format(parallelism), flush=True)
    write_summary(output, summary)
    # Prepare once before any simulation; workers cannot trigger forced rebuilds.
    prepare_log = output / 'launch/prepare.log'
    try:
        result = run_command(command_for('vcs-coverage-prepare', local), base_env, prepare_log, 600)
        if result['timed_out'] or result['returncode'] != 0:
            raise ValueError('coverage cache preparation failed; see ' + str(prepare_log))
        summary['preparation'] = dict(status='passed', log=str(prepare_log), **result)
        summary['build_sha256'] = build_fingerprints(base_env)
    except (ValueError, OSError) as exc:
        summary.update(status='failed', preparation=dict(status='failed', error=str(exc), log=str(prepare_log)),
                       merge=dict(status='skipped', error='coverage cache preparation failed'))
        for job in summary['jobs']:
            job['status'] = 'skipped'
        write_summary(output, summary)
        print(str(exc), flush=True)
        return summary
    base_env['TLTEST_VCS_REBUILD'] = '0'
    totals = None
    with ThreadPoolExecutor(max_workers=parallelism) as pool:
        pending = {}
        next_index = 0

        def submit(index):
            job = summary['jobs'][index]
            job['status'] = 'running'
            print('[{}/{}] {}: running (D.ready {})'.format(index + 1, len(suite['jobs']), job['name'],
                  'backpressure' if job['d_ready_backpressure']['enabled'] else 'always ready'), flush=True)
            future = pool.submit(run_job, suite['jobs'][index], base_env, coverage_dir, run_root, output, local)
            pending[future] = index

        while next_index < min(parallelism, len(suite['jobs'])):
            submit(next_index)
            next_index += 1
        write_summary(output, summary)
        while pending:
            finished, _ = wait(pending, return_when=FIRST_COMPLETED)
            for future in sorted(finished, key=lambda item: pending[item]):
                index = pending.pop(future)
                try:
                    job = future.result()
                except Exception as exc:
                    job = dict(summary['jobs'][index])
                    fail_job(job, 'worker failed: ' + str(exc), coverage_dir)
                if job['status'] == 'passed':
                    try:
                        if job.pop('build_sha256') != summary['build_sha256']:
                            raise ValueError('simulator, DPI library or coverage configuration changed during suite execution')
                        shape = {key: value['total'] for key, value in job['coverage'].items()}
                        if totals is not None and shape != totals:
                            raise ValueError('coverage denominator differs from previous jobs')
                        totals = shape
                    except ValueError as exc:
                        fail_job(job, exc, coverage_dir)
                summary['jobs'][index] = job
                print('  {}: {}{}'.format(job['name'], job['status'], ': ' + job['error'] if job['error'] else ''), flush=True)
                if next_index < len(suite['jobs']):
                    submit(next_index)
                    next_index += 1
                write_summary(output, summary)
    passing = [job for job in summary['jobs'] if job['status'] == 'passed']
    if passing:
        merge_log = output / 'launch/merge.log'
        try:
            expected = {Path(job['vdb']).resolve() for job in passing}
            actual = {p.resolve() for p in (coverage_dir / 'vdb').glob('*.vdb')}
            if actual != expected:
                raise ValueError('merge input does not match passing suite jobs')
            result = run_command(command_for('vcs-coverage-merge', local), base_env, merge_log, 600)
            if result['timed_out'] or result['returncode'] != 0:
                raise ValueError('coverage merge failed; see ' + str(merge_log))
            merged = read_coverage(coverage_dir / 'report_merged')
            if {key: value['total'] for key, value in merged.items()} != totals:
                raise ValueError('merged coverage denominator differs from individual runs')
            for key in METRICS:
                individual = [job['coverage'][key]['covered'] for job in passing]
                if not max(individual) <= merged[key]['covered'] <= min(totals[key], sum(individual)):
                    raise ValueError('merged ' + key + ' coverage is not a valid union of passing runs')
            summary.update(coverage=merged, merge=dict(status='passed', inputs=[str(p) for p in sorted(expected)], log=str(merge_log)))
        except (ValueError, OSError, ET.ParseError) as exc:
            summary['merge'] = dict(status='failed', error=str(exc), log=str(merge_log))
    else:
        summary['merge'] = dict(status='skipped', error='no passing jobs')
    summary['status'] = 'passed' if len(passing) == len(summary['jobs']) and summary['merge']['status'] == 'passed' else 'failed'
    write_summary(output, summary)
    print('Suite {}: {}/{} jobs passed'.format(summary['status'], len(passing), len(summary['jobs'])), flush=True)
    for key in METRICS:
        print('  {}: {}'.format(key, format_metric((summary.get('coverage') or {}).get(key))), flush=True)
    print('Summary: ' + str(output / 'summary.md'), flush=True)
    return summary


def positive_int(value):
    try:
        parsed = int(value)
    except ValueError:
        raise argparse.ArgumentTypeError('must be a positive integer')
    if parsed <= 0:
        raise argparse.ArgumentTypeError('must be a positive integer')
    return parsed


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('suite', nargs='?', default=str(PROJECT_ROOT / 'vcs/suites/full.json'),
                        help='JSON suite (default: vcs/suites/full.json); paths are relative to the project root')
    parser.add_argument('-j', '--jobs', type=positive_int, default=8, help='maximum concurrent jobs (default: 8; use 1 for serial)')
    parser.add_argument('--output', type=Path, help='new output directory (must not already exist)')
    parser.add_argument('--dry-run', action='store_true', help='validate and show all trace/INI settings without running')
    parser.add_argument('--local', action='store_true', help='run make locally inside an initialized EDA environment')
    args = parser.parse_args(argv)
    try:
        suite = load_suite(args.suite)
        suite['parallelism'] = args.jobs
        if args.dry_run:
            print(json.dumps(suite, ensure_ascii=False, indent=2))
            return 0
        output = args.output or PROJECT_ROOT / 'build_vcs/suites' / suite['name'] / (
            datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ') + '-' + uuid.uuid4().hex[:8])
        return 0 if run_suite(suite, output, args.local, args.jobs)['status'] == 'passed' else 1
    except (ValueError, OSError, configparser.Error, ET.ParseError) as exc:
        print('error: ' + str(exc), file=sys.stderr)
        return 2


if __name__ == '__main__':
    sys.exit(main())
