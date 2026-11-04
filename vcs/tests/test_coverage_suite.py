#!/usr/bin/env python3
"""Integration tests of suite orchestration using a fake EDA launcher, never VCS."""
from concurrent.futures import ThreadPoolExecutor
import importlib.util
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import tempfile
import time
import unittest


VCS_DIR = Path(__file__).resolve().parents[1]
RUNNER = VCS_DIR / "run_coverage_suite.py"


FAKE_LAUNCHER = r'''#!/usr/bin/env python3
import atexit
import configparser
import json
import os
from pathlib import Path
import re
import sys
import time

root = Path(__file__).resolve().parents[1]
args = sys.argv[1:]
values = dict(a.split("=", 1) for a in args[1:] if "=" in a)
coverage = Path(os.environ["TLTEST_VCS_COVERAGE_DIR"])
record = {"args": args, "env": {k: v for k, v in os.environ.items() if k.startswith("TLTEST_")}}
with (root / "calls.jsonl").open("a") as out:
    out.write(json.dumps(record) + "\n")


def event(stage, name):
    with (root / "events.jsonl").open("a") as out:
        out.write(json.dumps({"stage": stage, "name": name, "time": time.monotonic()}) + "\n")


def report(path, covered=4):
    path.mkdir(parents=True, exist_ok=True)
    (path / "session.xml").write_text(
        '<session><scope type="top" name="top">'
        '<metric name="Line" value="%s/10"/>'
        '<metric name="Cond" value="%s/20"/>'
        '<metric name="Toggle" value="%s/30"/>'
        '</scope></session>' % (covered, covered, covered))
    (path / "dashboard.html").write_text("fake coverage report\n")


if args[0] == "vcs-coverage-prepare":
    if os.environ.get("FAKE_PREPARE_FAIL") == "1":
        sys.exit(9)
    print("Fake compile cache prepared")
    sys.exit(0)


if args[0] == "vcs-coverage-merge":
    event("merge", "merge")
    if os.environ.get("FAKE_MERGE_FAIL") == "1":
        sys.exit(7)
    names = sorted(p.stem for p in (coverage / "vdb").glob("*.vdb"))
    (root / "merged-names.json").write_text(json.dumps(names))
    covered = int(os.environ.get("FAKE_MERGE_COVERED", str(min(8, 4 * len(names)))))
    report(coverage / "report_merged", covered)
    print("VCS merged coverage report:", coverage / "report_merged/dashboard.html")
    sys.exit(0)

if args[0] not in ("vcs-coverage-case", "vcs-coverage-fuzz"):
    raise SystemExit("unexpected fake launcher target: " + repr(args))
fuzz_job = args[0] == "vcs-coverage-fuzz"
name = values["NAME"]
event("start", name)
atexit.register(lambda: event("end", name))
delays = json.loads(os.environ.get("FAKE_CASE_DELAYS", "{}"))
time.sleep(float(delays.get(name, os.environ.get("FAKE_CASE_DELAY", "0"))))
ini = Path(values["INI"])
trace = Path(values["TRACE_FILE"]) if not fuzz_job else None
run = Path(os.environ["TLTEST_VCS_RUN_ROOT"] + "_" + name)
run.mkdir(parents=True, exist_ok=True)
text = ini.read_text()
if not fuzz_job:
    text = re.sub(r"(?m)^trace\.filepath\s*=.*$", "trace.filepath = " + str(trace), text)
(run / "tltest.ini").write_text(text)
config = configparser.ConfigParser(interpolation=None)
config.read(ini)
mode = config.get("fake", "mode", fallback="pass")
(coverage / "vdb" / (name + ".vdb")).mkdir(parents=True, exist_ok=True)
if mode == "timeout":
    # The real backend bounds the simulator separately from the SSH/tooling
    # allowance. Emulate its timeout status without waiting for that allowance.
    if float(os.environ.get("TLTEST_VCS_TIMEOUT", "0")) <= 0:
        raise SystemExit("suite did not pass a simulator timeout")
    (run / "simv.log").write_text("simulator timed out\n")
    sys.exit(124)
if mode == "fatal":
    (run / "simv.log").write_text("Fatal: assertion failure\n")
    sys.exit(0)
if mode == "missing-finished":
    (run / "simv.log").write_text("simulation stopped early\n")
    sys.exit(0)
if mode == "nonzero":
    (run / "simv.log").write_text("launcher failed\n")
    sys.exit(9)
if mode == "bad-counts":
    summary = "sent{R/W/E}=1/0/0, recv{R/W/E}=0/0/0, pending_entries=1"
else:
    summary = "sent{R/W/E}=1/0/0, recv{R/W/E}=1/0/0, pending_entries=0"
(run / "simv.log").write_text(
    ("[98] [TRC] Trace summary: " + summary + "\n" if not fuzz_job else "")
    + ("[100] [tl-test-new-FINAL] tl-test subsystem FINISHED.\n"
     if mode != "spurious-finished" else "[100] note: FINISHED is only a marker in this message\n"))
report(coverage / "reports" / name)
print("VCS coverage report:", coverage / "reports" / name / "dashboard.html")
'''


class CoverageSuiteTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="tltest-suite-test-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        (self.root / "vcs").mkdir()
        (self.root / "traces").mkdir()
        (self.root / "configs").mkdir()
        self.assertTrue(RUNNER.is_file(), "suite runner is missing")
        for source in VCS_DIR.glob("*.py"):
            shutil.copy2(source, self.root / "vcs" / source.name)
        fake = self.root / "vcs/run_vcs_remote.sh"
        fake.write_text(FAKE_LAUNCHER)
        fake.chmod(0o755)
        self.trace = self.root / "traces/shared.ops"
        self.trace.write_text("LD 0x1000 0\nFE 0x0 0\n")
        self.make_ini("normal")
        self.make_ini("backpressure", backpressure=True)
        self.output = self.root / "output"

    def make_ini(self, name, mode="pass", backpressure=False, trace_enabled=True):
        path = self.root / "configs" / (name + ".ini")
        settings = ""
        if backpressure:
            settings = "cagent.d_ready.period = 256\ncagent.d_ready.low = 192\ncagent.d_ready.start = 0\n"
        path.write_text(
            "[tltest.config]\ncore = 1\n" + settings
            + "[tltest.fuzzer]\ntrace.enable = " + ("1" if trace_enabled else "0") + "\n"
            + ("trace.filepath = unused.ops\n" if trace_enabled else "")
            + "[fake]\nmode = " + mode + "\n")
        return path

    def job(self, name, ini="normal", **extra):
        return dict(name=name, trace="traces/shared.ops", ini="configs/" + ini + ".ini", **extra)

    def run_suite(self, jobs, *, defaults=None, dry_run=False, env=None, raw=None, default_suite=False, jobs_limit=None, jobs_flag="--jobs"):
        suite = self.root / "vcs/suites/full.json" if default_suite else self.root / "suite.json"
        suite.parent.mkdir(parents=True, exist_ok=True)
        data = raw if raw is not None else dict(
            version=1, name="test-suite", defaults=defaults or {}, jobs=jobs)
        suite.write_text(json.dumps(data))
        cmd = [sys.executable, str(self.root / "vcs/run_coverage_suite.py")]
        if not default_suite:
            cmd.append(str(suite))
        cmd += ["--output", str(self.output)]
        if jobs_limit is not None:
            cmd += [jobs_flag, str(jobs_limit)]
        if dry_run:
            cmd.append("--dry-run")
        child_env = os.environ.copy()
        if env:
            child_env.update(env)
        self.result = subprocess.run(cmd, cwd=self.root, env=child_env, text=True,
                                     stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                     timeout=15)
        return self.result

    def calls(self):
        path = self.root / "calls.jsonl"
        return [json.loads(s) for s in path.read_text().splitlines()] if path.exists() else []

    def events(self):
        path = self.root / "events.jsonl"
        return sorted([json.loads(s) for s in path.read_text().splitlines()], key=lambda row: row["time"]) if path.exists() else []

    def peak_simulations(self):
        active = peak = 0
        for event in self.events():
            if event["stage"] == "start":
                active += 1
                peak = max(peak, active)
            elif event["stage"] == "end":
                active -= 1
                self.assertGreaterEqual(active, 0)
        self.assertEqual(0, active)
        return peak

    def result_json(self):
        return json.loads((self.output / "summary.json").read_text())

    def assert_no_launch(self):
        self.assertEqual([], self.calls(), self.result.stdout)

    def test_same_trace_distinct_configs_are_independent_and_merge_both(self):
        result = self.run_suite([self.job("normal"), self.job("pressure", "backpressure")])
        self.assertEqual(0, result.returncode, result.stdout)
        launches = [c for c in self.calls() if c["args"][0] == "vcs-coverage-case"]
        self.assertEqual(2, len(launches), result.stdout)
        by_name = {next(a.split("=", 1)[1] for a in c["args"] if a.startswith("NAME=")): c for c in launches}
        self.assertIn("INI=" + str(self.root / "configs/normal.ini"), by_name["normal"]["args"])
        self.assertIn("INI=" + str(self.root / "configs/backpressure.ini"), by_name["pressure"]["args"])
        self.assertEqual(["normal", "pressure"], json.loads((self.root / "merged-names.json").read_text()))
        self.assertTrue((self.output / "summary.md").is_file())
        self.assertTrue((self.output / "coverage/report_merged/session.xml").is_file())
        summary = self.result_json()
        self.assertEqual("passed", summary["status"])
        self.assertEqual(8, summary["parallelism"])
        self.assertEqual([False, True], [r["d_ready_backpressure"]["enabled"] for r in summary["jobs"]])
        self.assertEqual(192, summary["jobs"][1]["d_ready_backpressure"]["low_ticks"])
        self.assertEqual(4, summary["jobs"][0]["coverage"]["Cond"]["covered"])
        self.assertEqual(8, summary["coverage"]["Cond"]["covered"])

    def test_fuzz_without_trace_summary_can_merge_with_trace_job(self):
        self.make_ini("random", trace_enabled=False)
        result = self.run_suite([
            self.job("directed"),
            dict(name="random", mode="fuzz", ini="configs/random.ini"),
        ])
        self.assertEqual(0, result.returncode, result.stdout)
        launches = [c for c in self.calls() if c["args"][0] in ("vcs-coverage-case", "vcs-coverage-fuzz")]
        self.assertCountEqual(["vcs-coverage-case", "vcs-coverage-fuzz"], [c["args"][0] for c in launches])
        fuzz_launch = next(c for c in launches if c["args"][0] == "vcs-coverage-fuzz")
        self.assertFalse(any(arg.startswith("TRACE_FILE=") for arg in fuzz_launch["args"]))
        self.assertEqual(["directed", "random"], json.loads((self.root / "merged-names.json").read_text()))
        summary = self.result_json()
        self.assertEqual("passed", summary["jobs"][1]["status"])
        self.assertEqual("fuzz", summary["jobs"][1]["mode"])
        self.assertEqual("fuzzer_finished", summary["jobs"][1]["completion_policy"])
        self.assertNotIn("sent_RWE", summary["jobs"][1])
        self.assertNotIn("Trace summary:", (self.output / "run/vcs_run_random/simv.log").read_text())

    def test_default_cli_manifest_runs_full_suite_file(self):
        result = self.run_suite([self.job("from-full")], default_suite=True)
        self.assertEqual(0, result.returncode, result.stdout)
        launch = next(c for c in self.calls() if c["args"][0] == "vcs-coverage-case")
        self.assertIn("NAME=from-full", launch["args"])
        self.assertEqual(["from-full"], json.loads((self.root / "merged-names.json").read_text()))

    def test_fuzz_mode_rejects_trace_enabled_ini(self):
        result = self.run_suite([dict(name="random", mode="fuzz", ini="configs/normal.ini")])
        self.assertNotEqual(0, result.returncode, result.stdout)
        self.assert_no_launch()

    def test_fuzz_mode_rejects_trace_field(self):
        self.make_ini("random", trace_enabled=False)
        result = self.run_suite([self.job("random", "random", mode="fuzz")])
        self.assertNotEqual(0, result.returncode, result.stdout)
        self.assert_no_launch()

    def test_default_trace_mode_requires_trace_file(self):
        result = self.run_suite([dict(name="directed", ini="configs/normal.ini")])
        self.assertNotEqual(0, result.returncode, result.stdout)
        self.assert_no_launch()

    def test_trace_mode_rejects_disabled_trace_ini(self):
        self.make_ini("random", trace_enabled=False)
        result = self.run_suite([self.job("directed", "random", mode="trace")])
        self.assertNotEqual(0, result.returncode, result.stdout)
        self.assert_no_launch()

    def test_jobs_two_overlap_bounded_and_keep_manifest_summary_order(self):
        names = ["first", "second", "third", "fourth"]
        result = self.run_suite([self.job(name) for name in names], jobs_limit=2, env={
            "FAKE_CASE_DELAYS": json.dumps({"first": 0.6, "second": 0.12, "third": 0.12, "fourth": 0.12}),
        })
        self.assertEqual(0, result.returncode, result.stdout)
        self.assertEqual(2, self.peak_simulations())
        summary = self.result_json()
        self.assertEqual(2, summary["parallelism"])
        self.assertEqual(names, [job["name"] for job in summary["jobs"]])
        ended = {e["name"]: e["time"] for e in self.events() if e["stage"] == "end"}
        self.assertLess(ended["second"], ended["first"])
        merge_time = next(e["time"] for e in self.events() if e["stage"] == "merge")
        self.assertGreaterEqual(merge_time, max(ended.values()))

    def test_default_parallelism_reaches_eight_without_exceeding_it(self):
        names = ["case{:02d}".format(i) for i in range(10)]
        result = self.run_suite([self.job(name) for name in names], env={"FAKE_CASE_DELAY": "0.5"})
        self.assertEqual(0, result.returncode, result.stdout)
        self.assertEqual(8, self.result_json()["parallelism"])
        self.assertEqual(8, self.peak_simulations())
        self.assertEqual(names, [job["name"] for job in self.result_json()["jobs"]])
        ended = [e["time"] for e in self.events() if e["stage"] == "end"]
        merge_time = next(e["time"] for e in self.events() if e["stage"] == "merge")
        self.assertGreaterEqual(merge_time, max(ended))

    def test_jobs_one_runs_serially(self):
        result = self.run_suite([self.job("one"), self.job("two"), self.job("three")],
                                jobs_limit=1, jobs_flag="-j", env={"FAKE_CASE_DELAY": "0.1"})
        self.assertEqual(0, result.returncode, result.stdout)
        self.assertEqual(1, self.peak_simulations())
        self.assertEqual(1, self.result_json()["parallelism"])

    def test_parallel_failed_job_is_excluded_after_all_workers_finish(self):
        self.make_ini("fatal", mode="fatal")
        result = self.run_suite([self.job("slow"), self.job("bad", "fatal"), self.job("good")],
                                jobs_limit=2, env={"FAKE_CASE_DELAYS": json.dumps({"slow": 0.4, "bad": 0.05, "good": 0.1})})
        self.assertNotEqual(0, result.returncode, result.stdout)
        self.assertEqual(2, self.peak_simulations())
        self.assertEqual(["good", "slow"], json.loads((self.root / "merged-names.json").read_text()))
        self.assertFalse((self.output / "coverage/vdb/bad.vdb").exists())
        ended = [e["time"] for e in self.events() if e["stage"] == "end"]
        merge_time = next(e["time"] for e in self.events() if e["stage"] == "merge")
        self.assertGreaterEqual(merge_time, max(ended))

    def test_prepare_failure_stops_before_any_simulation(self):
        result = self.run_suite([self.job("one"), self.job("two")], env={"FAKE_PREPARE_FAIL": "1"})
        self.assertNotEqual(0, result.returncode, result.stdout)
        self.assertEqual(["vcs-coverage-prepare"], [c["args"][0] for c in self.calls()])
        self.assertEqual("failed", self.result_json()["status"])
        self.assertFalse((self.root / "merged-names.json").exists())

    def test_rebuild_is_only_requested_during_single_prepare(self):
        result = self.run_suite([self.job("one"), self.job("two")], env={"TLTEST_VCS_REBUILD": "1"})
        self.assertEqual(0, result.returncode, result.stdout)
        prepares = [c for c in self.calls() if c["args"][0] == "vcs-coverage-prepare"]
        self.assertEqual(1, len(prepares))
        self.assertEqual("1", prepares[0]["env"].get("TLTEST_VCS_REBUILD"))
        workers = [c for c in self.calls() if c["args"][0] == "vcs-coverage-case"]
        self.assertEqual(2, len(workers))
        for worker in workers:
            self.assertNotEqual("1", worker["env"].get("TLTEST_VCS_REBUILD"))

    def test_jobs_zero_and_negative_are_rejected_before_outputs(self):
        for value in [0, -1]:
            with self.subTest(jobs=value):
                result = self.run_suite([self.job("good")], jobs_limit=value)
                self.assertNotEqual(0, result.returncode, result.stdout)
                self.assert_no_launch()
                self.assertFalse(self.output.exists())

    def test_zero_exit_fatal_is_quarantined_and_only_passed_case_merges(self):
        self.make_ini("fatal", mode="fatal")
        result = self.run_suite([self.job("bad", "fatal"), self.job("good")])
        self.assertNotEqual(0, result.returncode, result.stdout)
        self.assertEqual(["good"], json.loads((self.root / "merged-names.json").read_text()))
        self.assertFalse((self.output / "coverage/vdb/bad.vdb").exists())
        self.assertTrue(any(self.output.rglob("bad.vdb")), result.stdout)
        self.assertTrue((self.output / "summary.json").is_file())

    def test_zero_exit_without_finished_is_not_successful(self):
        self.make_ini("unfinished", mode="missing-finished")
        result = self.run_suite([self.job("unfinished", "unfinished")])
        self.assertNotEqual(0, result.returncode, result.stdout)
        self.assertFalse((self.root / "merged-names.json").exists())
        self.assertFalse((self.output / "coverage/vdb/unfinished.vdb").exists())
        self.assertTrue((self.output / "summary.json").is_file())

    def test_timeout_excludes_case_and_continues(self):
        self.make_ini("timeout", mode="timeout")
        result = self.run_suite([self.job("slow", "timeout", timeout_seconds=1), self.job("good")])
        self.assertNotEqual(0, result.returncode, result.stdout)
        self.assertEqual(["good"], json.loads((self.root / "merged-names.json").read_text()))
        self.assertFalse((self.output / "coverage/vdb/slow.vdb").exists())

    def test_duplicate_case_names_fail_before_launch(self):
        result = self.run_suite([self.job("same"), self.job("same", "backpressure")])
        self.assertNotEqual(0, result.returncode, result.stdout)
        self.assert_no_launch()

    def test_missing_trace_fails_before_any_case_launches(self):
        jobs = [self.job("good"), dict(name="missing", trace="traces/absent.ops", ini="configs/normal.ini")]
        result = self.run_suite(jobs)
        self.assertNotEqual(0, result.returncode, result.stdout)
        self.assert_no_launch()

    def test_missing_ini_fails_before_launch(self):
        result = self.run_suite([self.job("bad", "absent")])
        self.assertNotEqual(0, result.returncode, result.stdout)
        self.assert_no_launch()

    def test_dry_run_does_not_launch_or_generate_coverage(self):
        result = self.run_suite([self.job("normal"), self.job("pressure", "backpressure")], dry_run=True)
        self.assertEqual(0, result.returncode, result.stdout)
        self.assert_no_launch()
        self.assertFalse((self.output / "coverage").exists())

    def test_job_configuration_overrides_inherited_stimulus_environment(self):
        result = self.run_suite([self.job("good")], env={
            "TLTEST_VCS_TRACE": "/do/not/use.ops",
            "TLTEST_VCS_INI": "/do/not/use.ini",
            "TLTEST_VCS_RUN_ARGS": "+unintended=1",
            "TLTEST_VCS_COVERAGE_DIR": "/do/not/write/coverage",
            "TLTEST_VCS_RUN_ROOT": "/do/not/write/run",
        })
        self.assertEqual(0, result.returncode, result.stdout)
        launch = next(c for c in self.calls() if c["args"][0] == "vcs-coverage-case")
        self.assertNotEqual("/do/not/use.ops", launch["env"].get("TLTEST_VCS_TRACE"))
        self.assertNotEqual("/do/not/use.ini", launch["env"].get("TLTEST_VCS_INI"))
        self.assertNotIn("+unintended=1", launch["env"].get("TLTEST_VCS_RUN_ARGS", ""))
        self.assertEqual(str(self.output / "coverage"), launch["env"]["TLTEST_VCS_COVERAGE_DIR"])
        self.assertEqual(str(self.output / "run/vcs_run"), launch["env"]["TLTEST_VCS_RUN_ROOT"])

    def test_existing_output_is_refused_without_modification(self):
        self.output.mkdir()
        marker = self.output / "preserve-me.txt"
        marker.write_text("previous run")
        result = self.run_suite([self.job("good")])
        self.assertNotEqual(0, result.returncode, result.stdout)
        self.assert_no_launch()
        self.assertEqual("previous run", marker.read_text())

    def test_invalid_backpressure_schedule_fails_preflight(self):
        path = self.make_ini("invalid", backpressure=True)
        path.write_text(path.read_text().replace("period = 256", "period = 192"))
        result = self.run_suite([self.job("good"), self.job("bad", "invalid")])
        self.assertNotEqual(0, result.returncode, result.stdout)
        self.assert_no_launch()

    def test_repeated_ini_section_is_rejected_before_launch(self):
        path = self.make_ini("duplicate-section", backpressure=True)
        path.write_text(path.read_text()
                        + "[tltest.config]\ncagent.d_ready.period = 0\ncagent.d_ready.low = 0\n")
        result = self.run_suite([self.job("ambiguous", "duplicate-section")], dry_run=True)
        self.assertEqual(2, result.returncode, result.stdout)
        self.assertIn("tltest.config", result.stdout)
        self.assert_no_launch()
        self.assertFalse(self.output.exists())

    def test_repeated_ini_key_is_rejected_before_launch(self):
        path = self.make_ini("duplicate-key", backpressure=True)
        path.write_text(path.read_text().replace(
            "cagent.d_ready.low = 192\n", "cagent.d_ready.low = 192\ncagent.d_ready.low = 0\n"))
        result = self.run_suite([self.job("ambiguous", "duplicate-key")], dry_run=True)
        self.assertEqual(2, result.returncode, result.stdout)
        self.assertIn("cagent.d_ready.low", result.stdout)
        self.assert_no_launch()
        self.assertFalse(self.output.exists())

    def test_paths_with_spaces_remain_single_arguments(self):
        trace = self.root / "traces/with spaces.ops"
        trace.write_text(self.trace.read_text())
        ini = self.make_ini("with spaces")
        result = self.run_suite([dict(name="spaces", trace="traces/with spaces.ops", ini="configs/with spaces.ini")])
        self.assertEqual(0, result.returncode, result.stdout)
        launch = next(c for c in self.calls() if c["args"][0] == "vcs-coverage-case")
        self.assertIn("TRACE_FILE=" + str(trace), launch["args"])
        self.assertIn("INI=" + str(ini), launch["args"])

    def test_merge_failure_returns_failure_and_keeps_summary(self):
        result = self.run_suite([self.job("good")], env={"FAKE_MERGE_FAIL": "1"})
        self.assertNotEqual(0, result.returncode, result.stdout)
        self.assertTrue((self.output / "summary.json").is_file())

    def test_merged_coverage_cannot_lose_passed_job_hits(self):
        result = self.run_suite([self.job("good")], env={"FAKE_MERGE_COVERED": "3"})
        self.assertNotEqual(0, result.returncode, result.stdout)
        summary = self.result_json()
        self.assertEqual("passed", summary["jobs"][0]["status"])
        self.assertEqual("failed", summary["merge"]["status"])

    def test_merged_coverage_cannot_exceed_sum_of_passed_hits(self):
        result = self.run_suite([self.job("good")], env={"FAKE_MERGE_COVERED": "5"})
        self.assertNotEqual(0, result.returncode, result.stdout)
        self.assertEqual("failed", self.result_json()["merge"]["status"])

    def test_unrelated_finished_word_is_not_completion(self):
        self.make_ini("spurious", mode="spurious-finished")
        result = self.run_suite([self.job("spurious", "spurious")])
        self.assertNotEqual(0, result.returncode, result.stdout)
        self.assertFalse((self.root / "merged-names.json").exists())

    def test_missing_trace_filepath_fails_preflight(self):
        path = self.make_ini("missing-path")
        path.write_text(path.read_text().replace("trace.filepath = unused.ops\n", ""))
        result = self.run_suite([self.job("bad", "missing-path")])
        self.assertNotEqual(0, result.returncode, result.stdout)
        self.assert_no_launch()

    def test_outer_command_timeout_reports_failure(self):
        spec = importlib.util.spec_from_file_location("coverage_suite_under_test", RUNNER)
        runner = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(runner)
        result = runner.run_command(
            [sys.executable, "-c", "import time; time.sleep(2)"],
            os.environ.copy(), self.root / "command.log", 0.05)
        self.assertTrue(result["timed_out"])
        self.assertIsNone(result["returncode"])

    def assert_timeout_stops_process_tree(self, leader_ignores_term):
        spec = importlib.util.spec_from_file_location("coverage_suite_under_test", RUNNER)
        runner = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(runner)
        runner.TERMINATE_GRACE_SECONDS = 0.1
        script = self.root / "process_tree.py"
        pids = self.root / "processes.jsonl"
        heartbeat = self.root / "heartbeat"
        script.write_text(r'''import json, os, signal, subprocess, sys, time
from pathlib import Path
role, pidfile, heartbeat, ignore_leader = sys.argv[1:]
if role != "launcher" or ignore_leader == "1":
    signal.signal(signal.SIGTERM, signal.SIG_IGN)
with Path(pidfile).open("a") as out:
    out.write(json.dumps({"role": role, "pid": os.getpid()}) + "\n")
if role != "grandchild":
    subprocess.Popen([sys.executable, __file__, "child" if role == "launcher" else "grandchild",
                      pidfile, heartbeat, ignore_leader])
while True:
    if role == "grandchild":
        Path(heartbeat).write_text(str(time.monotonic_ns()))
    time.sleep(0.02)
''')
        try:
            result = runner.run_command(
                [sys.executable, str(script), "launcher", str(pids), str(heartbeat),
                 "1" if leader_ignores_term else "0"],
                os.environ.copy(), self.root / "tree.log", 1)
            self.assertTrue(result["timed_out"])
            self.assertIsNone(result["returncode"])
            processes = [json.loads(line) for line in pids.read_text().splitlines()]
            self.assertCountEqual(["launcher", "child", "grandchild"], [p["role"] for p in processes])
            self.assertTrue(heartbeat.is_file(), "grandchild did not start before timeout")
            last_heartbeat = heartbeat.read_text()
            time.sleep(0.15)
            self.assertEqual(last_heartbeat, heartbeat.read_text(), "descendant kept writing after timeout")
            for process in processes:
                state_file = Path("/proc") / str(process["pid"]) / "stat"
                if state_file.exists():
                    self.assertEqual("Z", state_file.read_text().rsplit(")", 1)[1].split()[0],
                                     "descendant is still running after timeout")
        finally:
            # Clean up the regression fixture even when testing the broken implementation.
            if pids.exists():
                for line in pids.read_text().splitlines():
                    try:
                        os.kill(json.loads(line)["pid"], signal.SIGKILL)
                    except ProcessLookupError:
                        pass

    def test_outer_timeout_kills_descendants_after_launcher_exits_on_term(self):
        self.assert_timeout_stops_process_tree(leader_ignores_term=False)

    def test_outer_timeout_kills_process_tree_that_ignores_term(self):
        self.assert_timeout_stops_process_tree(leader_ignores_term=True)

    def test_outer_timeout_does_not_stop_another_concurrent_launcher(self):
        spec = importlib.util.spec_from_file_location("coverage_suite_under_test", RUNNER)
        runner = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(runner)
        marker = self.root / "other-job-finished"
        with ThreadPoolExecutor(max_workers=2) as pool:
            timed_out = pool.submit(
                runner.run_command, [sys.executable, "-c", "import time; time.sleep(5)"],
                os.environ.copy(), self.root / "timed-out.log", 0.1)
            successful = pool.submit(
                runner.run_command,
                [sys.executable, "-c",
                 "from pathlib import Path; import sys,time; time.sleep(0.5); Path(sys.argv[1]).touch()",
                 str(marker)],
                os.environ.copy(), self.root / "successful.log", 3)
            self.assertTrue(timed_out.result()["timed_out"])
            self.assertEqual(dict(returncode=0, timed_out=False), successful.result())
        self.assertTrue(marker.is_file())

    def test_mismatched_transaction_counts_fail(self):
        self.make_ini("counts", mode="bad-counts")
        result = self.run_suite([self.job("counts", "counts")])
        self.assertNotEqual(0, result.returncode, result.stdout)
        self.assertFalse((self.root / "merged-names.json").exists())


if __name__ == "__main__":
    unittest.main()
