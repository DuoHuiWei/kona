#!/usr/bin/env python3
"""Run manual Cong/PCM layouts serially in kona-dev and compare adjacent layouts."""

import argparse
from collections import defaultdict
from contextlib import ExitStack
import csv
from datetime import datetime, timezone
import fcntl
import hashlib
import json
import math
import os
from pathlib import Path
import shlex
import signal
import statistics
import subprocess
import time

from kona_network_validation import NETWORK_CONFIG, check_network, network_contract

ROOT = Path(__file__).resolve().parents[1]
REMOTE = "/usr/src/Garnet"
TARGET = "kona-cut-v3-bench.x"
DOCKER = ["docker", "exec", "kona-dev"]
PHASES = ("block_compare", "scs", "matrix_compare", "rank_mask", "local", "online")
COUNTERS = ("sent_bytes", "theoretical_rounds", "exchange_rounds", "transport_actions",
            "dcf_evaluate_calls", "items")


def cli():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--n", type=int, required=True)
    parser.add_argument("--k", type=int, required=True)
    parser.add_argument("--profile", choices=NETWORK_CONFIG, default="wan")
    parser.add_argument("--repeats", type=int, default=1)
    parser.add_argument("--timeout", type=int, default=600)
    parser.add_argument("--chunking", choices=("on", "off"), default="off")
    parser.add_argument("--blocks", action="append", default=[])
    parser.add_argument("--block-sizes", action="append", default=[])
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()
    if not 1 <= args.k <= args.n <= 2147483647 or min(args.repeats, args.timeout) < 1:
        parser.error("require 1 <= k <= n and positive repeats/timeout")

    cases = []
    for flag, values in (("--blocks", args.blocks), ("--block-sizes", args.block_sizes)):
        for value in values:
            try:
                parts = value.split(",")
                if any(not item.isascii() or not item.isdecimal() or
                       not 1 <= int(item) <= 2147483647 for item in parts):
                    raise ValueError("layout values must be positive 32-bit integers")
                parts = list(map(int, parts))
                if flag == "--blocks":
                    if len(parts) != 1 or parts[0] > args.n:
                        raise ValueError("--blocks takes one count in [1, n]")
                    count = parts[0]
                    target = (args.n + count - 1) // count
                    length = args.n if count == 1 else 1 << (target - 1).bit_length()
                    full, tail = divmod(args.n, length)
                    parts = [length] * full + ([tail] if tail else [])
                    if len(parts) != count:
                        raise ValueError(f"requested {count} blocks, generated {parts}; use --block-sizes")
                if sum(parts) != args.n:
                    raise ValueError("block sizes must sum to n")
            except ValueError as error:
                parser.error(str(error))
            cases.append(dict(option=flag, value=value, layout=",".join(map(str, parts))))
    if not cases or len({case["layout"] for case in cases}) != len(cases):
        parser.error("supply at least one layout, with no duplicate effective layouts")
    cases.sort(key=lambda case: case["layout"].count(","))
    for index, case in enumerate(cases, 1):
        case["case"] = f"layout-{index:02d}"
    return args, cases


def output(command):
    return subprocess.check_output(command, text=True, timeout=30)


def save_json(path, value):
    path.write_text(json.dumps(value, indent=2) + "\n")


def write_csv(path, rows):
    if rows:
        with path.open("w", newline="") as stream:
            writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
            writer.writeheader()
            writer.writerows(rows)


def source_hashes():
    files = list((ROOT / "Machines").glob("kona-*.hpp"))
    files += list((ROOT / "Networking").glob("*.cpp")) + list((ROOT / "Networking").glob("*.h"))
    files += [ROOT / name for name in (
        "Machines/kona-cut-v3-bench.cpp", "Makefile", "Tools/octetStream.cpp",
        "Tools/octetStream.h", "Scripts/run-cut-v3-bench.py", "Scripts/kona-bench-log.sh",
        "Scripts/codex-container-build.sh", "Scripts/kona_network_validation.py",
        "Scripts/container-network.sh")]
    return {str(path.relative_to(ROOT)): hashlib.sha256(path.read_bytes()).hexdigest()
            for path in sorted(files)}


def parse(text, expected):
    meta, phases, batches = {}, {}, []
    for line in text.splitlines():
        if line.startswith(("CUT_PHASE ", "CUT_BATCH ")):
            kind, *fields = line.split()
            row = dict(field.split("=", 1) for field in fields)
            for metric in COUNTERS:
                row[metric] = int(row[metric])
            for metric in ("online_ms", "ms_per_item"):
                row[metric] = float(row[metric])
                if not math.isfinite(row[metric]) or row[metric] < 0:
                    raise RuntimeError(f"invalid timing in {line}")
            if row["transport_actions"] != 2 * row["exchange_rounds"]:
                raise RuntimeError(f"transport action mismatch in {line}")
            if kind == "CUT_BATCH":
                batches.append(row)
            else:
                if row["phase"] in phases:
                    raise RuntimeError(f"duplicate phase {row['phase']}")
                phases[row["phase"]] = row
        elif "=" in line and " " not in line.split("=", 1)[0]:
            key, value = line.split("=", 1)
            meta[key] = value
    for name, value in expected.items():
        if meta.get(name) != str(value):
            raise RuntimeError(f"{name}: expected {value}, got {meta.get(name)}")
    if set(phases) != set(PHASES):
        raise RuntimeError("missing or unexpected phases")
    for metric in ("online_ms", *COUNTERS[:-1]):
        if abs(phases["online"][metric] - sum(phases[phase][metric] for phase in PHASES[:-1])) > 1e-6:
            raise RuntimeError(f"phase closure failed for {metric}")
    for phase in PHASES[:4]:
        entries = [row for row in batches if row["phase"] == phase]
        for metric in ("online_ms", *COUNTERS):
            if abs(phases[phase][metric] - sum(row[metric] for row in entries)) > 1e-6:
                raise RuntimeError(f"batch closure failed for {phase}/{metric}")
        for row in entries:
            width = int(row["width"])
            backend = ("SCS" if phase == "scs" else
                       "DCF" if width <= int(meta["threshold"]) else "PCR")
            if row["backend"] != backend or row["items"] != width:
                raise RuntimeError(f"unexpected backend or width in {phase}")
    return meta, phases, batches


def summarize(out, cases, trials, batches):
    write_csv(out / "phase_trials.csv", trials)
    write_csv(out / "batch_trials.csv", batches)
    groups = defaultdict(list)
    for row in trials:
        groups[row["case"], row["phase"]].append(row)
    summary = []
    for (case, phase), rows in groups.items():
        values = [row["online_ms"] for row in rows]
        mean = statistics.mean(values)
        sd = statistics.stdev(values) if len(values) > 1 else None
        summary.append(dict(case=case, layout=rows[0]["layout"], phase=phase,
                            samples=len(values), mean_ms=mean, median_ms=statistics.median(values),
                            sd_ms=sd, cv=sd / mean if sd is not None and mean else None,
                            min_ms=min(values), max_ms=max(values),
                            mean_sent_bytes=statistics.mean(row["sent_bytes"] for row in rows),
                            mean_items=statistics.mean(row["items"] for row in rows)))
    write_csv(out / "phase_summary.csv", summary)
    indexed = {(row["case"], row["trial"], row["phase"]): row for row in trials}
    deltas = []
    for before, after in zip(cases, cases[1:]):
        for trial in sorted({row["trial"] for row in trials}):
            for phase in PHASES:
                old = indexed.get((before["case"], trial, phase))
                new = indexed.get((after["case"], trial, phase))
                if old is not None and new is not None:
                    deltas.append(dict(before=before["layout"], after=after["layout"],
                                       trial=trial, phase=phase,
                                       saved_ms=old["online_ms"] - new["online_ms"],
                                       saved_bytes=old["sent_bytes"] - new["sent_bytes"],
                                       saved_items=old["items"] - new["items"]))
    write_csv(out / "adjacent_trials.csv", deltas)
    lines = ["# Cut-v3", "", "Each trial uses the party with the slower complete online time "
             "for all phase times. Positive deltas are savings.", "",
             "| Layout | Block compare ms | SCS ms | Matrix compare ms | Rank-mask ms | Local ms | Online ms |",
             "|---|---:|---:|---:|---:|---:|---:|"]
    means = {(row["case"], row["phase"]): row["mean_ms"] for row in summary}
    for case in cases:
        if (case["case"], "online") in means:
            values = [means[case["case"], phase] for phase in PHASES]
            lines.append(f"| {case['layout']} | " + " | ".join(f"{v:.6f}" for v in values) + " |")
    lines += ["", "| Adjacent layouts | Block compare saved ms | SCS saved ms | Matrix compare saved ms | "
              "Rank-mask saved ms | Local saved ms | Online saved ms |",
              "|---|---:|---:|---:|---:|---:|---:|"]
    for before, after in zip(cases, cases[1:]):
        paired = [row for row in deltas if (row["before"], row["after"]) ==
                  (before["layout"], after["layout"])]
        if paired:
            values = [statistics.mean(row["saved_ms"] for row in paired if row["phase"] == phase)
                      for phase in PHASES]
            lines.append(f"| {before['layout']} → {after['layout']} | " +
                         " | ".join(f"{v:.6f}" for v in values) + " |")
    (out / "summary.md").write_text("\n".join(lines) + "\n")


def run_pair(out, job, runtime, bench_args, timeout, revision):
    processes = []
    marker = f"{out.name}/{job}"
    with ExitStack() as stack:
        try:
            for party in (0, 1):
                log = stack.enter_context((out / "raw" / f"{job}-P{party}.log").open("w"))
                args = ["env", f"KONA_RUN_ID={marker}", f"KONA_SOURCE_REVISION={revision}",
                        "KONA_SOURCE_DIRTY=True",
                        f"KONA_BENCH_SOURCE={REMOTE}/Machines/kona-cut-v3-bench.cpp",
                        f"KONA_BENCH_BINARY={REMOTE}/{TARGET}", "bash",
                        f"{REMOTE}/Scripts/kona-bench-log.sh", "timeout", "--kill-after=5",
                        str(timeout), f"{REMOTE}/{TARGET}", "-p", str(party), *bench_args]
                processes.append(subprocess.Popen(
                    [*DOCKER, "bash", "-lc", f"cd {shlex.quote(runtime)} && {shlex.join(args)}"],
                    stdout=log, stderr=subprocess.STDOUT))
            deadline = time.monotonic() + timeout + 15
            while True:
                codes = [process.poll() for process in processes]
                if codes == [0, 0]:
                    return
                if any(code not in (None, 0) for code in codes):
                    raise RuntimeError(f"{job}: party exits {codes}; see {out / 'raw'}")
                if time.monotonic() >= deadline:
                    raise TimeoutError(job)
                time.sleep(0.1)
        finally:
            if any(process.poll() is None for process in processes):
                kill = """
import os, pathlib, signal, sys
marker = ('KONA_RUN_ID=' + sys.argv[1]).encode()
for path in pathlib.Path('/proc').glob('[0-9]*/environ'):
    try:
        if marker in path.read_bytes().split(b'\\0'):
            os.kill(int(path.parent.name), signal.SIGKILL)
    except OSError:
        pass
"""
                subprocess.run([*DOCKER, "python3", "-c", kill, marker], check=False, timeout=15)
                for process in processes:
                    if process.poll() is None:
                        process.terminate()
                    process.wait(timeout=10)


def main():
    args, cases = cli()
    os.chdir(ROOT)
    env = dict(os.environ, KONA_CONTAINER="kona-dev", KONA_NETWORK_PRIVILEGED="1")
    jobs = [(trial, case) for trial in range(1, args.repeats + 1)
            for case in (cases if trial % 2 else reversed(cases))]
    contract = network_contract(args.profile, "serialized", container="kona-dev")
    manifest = dict(n=args.n, k=args.k, profile=contract, chunking=args.chunking,
                    repeats=args.repeats, cpu=[0, 2], core_exclusivity="NOT_VERIFIED",
                    jobs=[dict(trial=trial, **case) for trial, case in jobs])
    if args.dry_run:
        print(json.dumps(manifest, indent=2))
        return

    def interrupted(signum, _frame):
        raise KeyboardInterrupt(f"signal {signum}")

    signal.signal(signal.SIGTERM, interrupted)
    with open("/tmp/kona-bench-experiment.lock", "a+") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
        out = ROOT / "KNN-experiment-res" / f"cut-v3-{stamp}-{os.getpid()}"
        (out / "raw").mkdir(parents=True)
        print(f"RESULT_DIRECTORY={out}", flush=True)
        hashes = source_hashes()
        revision = output(["git", "rev-parse", "HEAD"]).strip()
        manifest.update(source_revision=revision, source_sha256=hashes)
        save_json(out / "manifest.json", manifest)
        touched = False
        completed = 0
        error = None
        cleanup = []
        try:
            with (out / "build.log").open("w") as log:
                for command in (["bash", "Scripts/codex-container-build.sh"],
                                [*DOCKER, "bash", "-lc", f"cd {REMOTE} && make -j4 {TARGET}"]):
                    subprocess.run(command, env=env, stdout=log, stderr=subprocess.STDOUT, check=True)
            qdisc = output([*DOCKER, "tc", "qdisc", "show", "dev", "lo"])
            if any(not line.startswith("qdisc noqueue ") for line in qdisc.splitlines()):
                raise RuntimeError("existing network shaping on lo")
            offloads = output([*DOCKER, "ethtool", "-k", "lo"])
            features = {"tso": "tcp-segmentation-offload", "gso": "generic-segmentation-offload",
                        "gro": "generic-receive-offload"}
            flags = [item for key, feature in features.items() for item in
                     (key, next(line.split()[1] for line in offloads.splitlines()
                                if line.startswith(feature + ":")))]
            setup = """
from pathlib import Path
import tempfile
runtime = Path(tempfile.mkdtemp(prefix='kona-cut-v3-'))
names = ('k0', 'k1', 'r0', 'r1')
keys = next((Path(base) / sub for base in
             ('/usr/src/Garnet', '/tmp/kona-accounting-regression', '/tmp/kona-reference-regression')
             for sub in ('Player-Data/2-fss', 'Player-Data')
             if all((Path(base) / sub / f).is_file() and (Path(base) / sub / f).stat().st_size
                    for f in names)), None)
if keys is None:
    raise RuntimeError('existing DCF material k0/k1/r0/r1 not found')
destination = runtime / 'Player-Data/2-fss'
destination.mkdir(parents=True)
for f in names:
    (destination / f).symlink_to(keys / f)
print(runtime)
"""
            runtime = output([*DOCKER, "python3", "-c", setup]).strip()
            identity_cmd = [*DOCKER, "sha256sum", f"{REMOTE}/{TARGET}",
                            *(f"{runtime}/Player-Data/2-fss/{key}" for key in ("k0", "k1", "r0", "r1"))]
            identity = output(identity_cmd)
            manifest.update(runtime=runtime, binary_and_material_sha256=identity.splitlines())
            save_json(out / "manifest.json", manifest)
            network_cmd = ["bash", "Scripts/container-network.sh"]
            touched = True
            shaping = subprocess.run([*network_cmd, args.profile, "lo", "--pacing", "serialized"],
                                     text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=env)
            (out / "network-apply.log").write_text(shaping.stdout)
            shaping.check_returncode()

            def snapshot(label):
                state = output([*DOCKER, "bash", "-lc", "set -e; tc -s qdisc show dev lo; "
                                "tc -s class show dev lo; ethtool -k lo; ps -eo pid,psr,comm,stat; "
                                "cat /sys/devices/system/cpu/isolated"])
                (out / f"{label}-network.log").write_text(state)
                return check_network(state, args.profile, "serialized", shaping.stdout, contract)

            trials, batches, order, checks = [], [], [], []
            fixture = None
            for trial, case in jobs:
                name = f"t{trial:03d}-{case['case']}"
                print(f"START {name} {case['layout']}", flush=True)
                if source_hashes() != hashes or output(identity_cmd) != identity:
                    raise RuntimeError("source, build or material changed")
                order.append(dict(trial=trial, **case))
                save_json(out / "execution-order.json", order)
                checks.append(dict(job=name, boundary="before", **snapshot(f"{name}-before")))
                save_json(out / "network-validation.json", checks)
                bench = ["--n", str(args.n), "--k", str(args.k), "--profile", args.profile,
                         "--chunking", args.chunking, case["option"], case["value"]]
                run_pair(out, name, runtime, bench, args.timeout, revision)
                checks.append(dict(job=name, boundary="after", **snapshot(f"{name}-after")))
                save_json(out / "network-validation.json", checks)
                parties = []
                for party in (0, 1):
                    log = (out / "raw" / f"{name}-P{party}.log").read_text()
                    cpu = (0, 2)[party]
                    if f"BENCH_AFFINITY party={party} cpu={cpu} " not in log or "affinity_check=PASS" not in log:
                        raise RuntimeError("missing CPU affinity evidence")
                    meta, phases, layers = parse(log, dict(
                        n=args.n, k=args.k, profile=args.profile, chunking=args.chunking, party=party,
                        layout=case["layout"], correctness="PASS", accounting_check="PASS"))
                    if fixture is None:
                        fixture = meta["fixture_checksum"]
                    if meta["fixture_checksum"] != fixture:
                        raise RuntimeError("fixture changed across layouts or parties")
                    parties.append(phases)
                    batches.extend(dict(case=case["case"], layout=case["layout"], trial=trial,
                                        party=party, **layer) for layer in layers)
                if source_hashes() != hashes or output(identity_cmd) != identity:
                    raise RuntimeError("source, build or material changed during trial")
                slower = max(range(2), key=lambda index: parties[index]["online"]["online_ms"])
                for phase in PHASES:
                    rows = [party[phase] for party in parties]
                    if any(rows[0][metric] != rows[1][metric] for metric in COUNTERS):
                        raise RuntimeError(f"party counter mismatch for {phase}")
                    trials.append(dict(case=case["case"], layout=case["layout"], trial=trial,
                                       critical_party=slower, **rows[slower],
                                       P0_online_ms=rows[0]["online_ms"], P1_online_ms=rows[1]["online_ms"]))
                summarize(out, cases, trials, batches)
                completed += 1
        except BaseException as exc:
            error = str(exc) or type(exc).__name__
            raise
        finally:
            if touched:
                with (out / "network-cleanup.log").open("w") as log:
                    for command in (["bash", "Scripts/container-network.sh", "--clear", "--dev", "lo"],
                                    ["docker", "exec", "--privileged", "kona-dev",
                                     "ethtool", "-K", "lo", *flags]):
                        cleanup.append(subprocess.run(command, env=env, stdout=log,
                                                      stderr=subprocess.STDOUT).returncode)
            save_json(out / "status.json", dict(
                status="FAILED" if error or any(cleanup) else "COMPLETED", error=error,
                completed_pairs=completed, planned_pairs=len(jobs), cleanup=cleanup))
            if not error and any(cleanup):
                raise RuntimeError("network cleanup failed")
    print(f"COMPLETED={out}", flush=True)


if __name__ == "__main__":
    main()
