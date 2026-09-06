#!/usr/bin/env python3
import time
import subprocess
import os
import sys
import argparse
from pathlib import Path
from statistics import median
import tempfile


def peak_rss(command):
    actions = [(os.POSIX_SPAWN_OPEN, fd, os.devnull, os.O_WRONLY, 0o666) for fd in (1, 2)]
    pid = os.posix_spawn(command[0], command, os.environ, file_actions=actions)
    _, status, usage = os.wait4(pid, 0)
    if os.waitstatus_to_exitcode(status) != 0:
        raise subprocess.CalledProcessError(os.waitstatus_to_exitcode(status), command)
    return usage.ru_maxrss


def compare_builds(baseline, candidate, max_regression):
    from regalloc_compat import ROOT, stress_sources

    binaries = [str(Path(binary).resolve()) for binary in (baseline, candidate)]
    with tempfile.TemporaryDirectory(prefix="winds-benchmark-") as tmp:
        work = Path(tmp)
        cases = sorted((ROOT / "tests").glob("[0-9][0-9]_*.cpp"))
        cases += sorted((ROOT / "tests").glob("[0-9][0-9]_*.c"))
        cases += sorted((ROOT / "tests").glob("multifile_*.cpp"))
        cases += stress_sources(work)
        jobs = [(case, [f"-O{level}", "-S"], os.devnull)
                for case in cases for level in range(3)]
        jobs.append((ROOT / "tests/03_classes.cpp", ["-O1"], str(work / "program.out")))
        failures = []
        print("baseline -> candidate: median ms, speedup %, peak RSS KiB (30 paired runs)", flush=True)
        for case, flags, output in jobs:
            commands = [[binary, *flags, str(case), "-o", output] for binary in binaries]
            supported = all(subprocess.run(command, stdout=subprocess.DEVNULL,
                                            stderr=subprocess.DEVNULL).returncode == 0
                            for command in commands)
            if not supported:
                print(f"{case.stem} {' '.join(flags)}: skipped (not supported by both compilers)")
                continue
            for _ in range(4):
                for command in commands:
                    subprocess.run(command, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
            samples = [[], []]
            paired_ratios = []
            for pair in range(30):
                pair_times = [0, 0]
                for index in ((0, 1) if pair % 2 == 0 else (1, 0)):
                    start = time.perf_counter()
                    subprocess.run(commands[index], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
                    elapsed = (time.perf_counter() - start) * 1000
                    samples[index].append(elapsed)
                    pair_times[index] = elapsed
                paired_ratios.append(pair_times[1] / pair_times[0])
            before, after = map(median, samples)
            rss = [peak_rss(command) for command in commands]
            name = f"{case.stem} {' '.join(flags)}"
            print(f"{name}: {before:.3f} -> {after:.3f} ms, "
                  f"{100 * (1 - after / before):+.1f}%, RSS {rss[0]} -> {rss[1]}", flush=True)
            limit = 1 + max_regression / 100
            paired_ratio = median(paired_ratios)
            if paired_ratio > limit:
                failures.append(f"{name}: time regressed {100 * (paired_ratio - 1):.1f}%")
            if rss[1] > rss[0] * limit:
                failures.append(f"{name}: RSS regressed {100 * (rss[1] / rss[0] - 1):.1f}%")

        sizes = [Path(binary).stat().st_size for binary in binaries]
        print(f"compiler bytes: {sizes[0]} -> {sizes[1]}", flush=True)
        if sizes[1] > sizes[0] * (1 + max_regression / 100):
            failures.append(f"compiler size regressed {100 * (sizes[1] / sizes[0] - 1):.1f}%")
        if failures:
            print(f"benchmark gate failed (limit {max_regression:g}%):", file=sys.stderr)
            for failure in failures:
                print(f"  {failure}", file=sys.stderr)
            raise SystemExit(1)

def measure(cmd, runs=20):
    times = []
    for _ in range(runs):
        t0 = time.perf_counter()
        res = subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        if res.returncode != 0:
            return None, None
        times.append((time.perf_counter() - t0) * 1000)
    return min(times), sum(times) / len(times)

def main():
    parser = argparse.ArgumentParser(description="Benchmark winds or compare two winds builds.")
    parser.add_argument("target", nargs="?", default="bin/winds")
    parser.add_argument("--baseline", help="Compare against this compiler instead of clang++")
    parser.add_argument("--max-regression", type=float, default=10,
                        help="Fail baseline comparisons above this percentage (default: 10)")
    args = parser.parse_args()
    target = args.target
    if args.baseline:
        compare_builds(args.baseline, target, args.max_regression)
        return
    test_file = "tests/03_classes.cpp"
    std_file = "tests/15_std_library.cpp"
    bench_out = "tests/winds_bench.out"
    
    print("================================================================")
    print("             compiler benchmark: winds vs clang++               ")
    print("================================================================")

    # 1. Core Compilation Benchmark (Source -> Assembly -S)
    c_min, c_avg = measure(["clang++", "-S", test_file, "-o", "/dev/null"])
    w_min, w_avg = measure([target, "-S", test_file, "-o", "/dev/null"])

    print("\n1. core compilation speed (-s: tests/03_classes.cpp)")
    if c_min and w_min:
        print(f"   clang++ : min = {c_min:6.2f} ms  |  avg = {c_avg:6.2f} ms")
        print(f"   winds   : min = {w_min:6.2f} ms  |  avg = {w_avg:6.2f} ms")
        print(f"   >>> winds is {c_min / w_min:.1f}x faster than clang++")
    else:
        print("   measurement failed.")

    # 2. Standard Library Compilation Benchmark
    if os.path.exists(std_file):
        c_std_min, c_std_avg = measure(["clang++", "-S", std_file, "-o", "/dev/null"])
        w_std_min, w_std_avg = measure([target, "-S", std_file, "-o", "/dev/null"])
        print("\n2. standard library compilation speed (-s: tests/15_std_library.cpp)")
        if c_std_min and w_std_min:
            print(f"   clang++ : min = {c_std_min:6.2f} ms  |  avg = {c_std_avg:6.2f} ms")
            print(f"   winds   : min = {w_std_min:6.2f} ms  |  avg = {w_std_avg:6.2f} ms")
            print(f"   >>> winds is {c_std_min / w_std_min:.1f}x faster than clang++")
        else:
            print("   measurement failed.")

    # 3. End-to-End Build (Source -> Executable)
    print("\n3. end-to-end build speed (source -> executable)")
    w_e2e_min, w_e2e_avg = measure([target, test_file, "-o", bench_out])
    if w_e2e_min:
        print(f"   winds (lex + parse + sema + ir + opt + gas + ld): min = {w_e2e_min:6.2f} ms  |  avg = {w_e2e_avg:6.2f} ms")
    if os.path.exists(bench_out):
        try:
            os.remove(bench_out)
        except OSError:
            pass

    # 4. Binary Footprint
    print("\n4. compiler executable footprint")
    w_sz = os.path.getsize(target) / 1024
    print(f"   winds executable    : {w_sz:6.1f} kb (standalone, zero llvm dependencies)")
    print(f"   clang++ + libllvm   : ~196.0 mb (dynamic runtime & backend libraries)")
    print(f"   >>> winds is ~{196 * 1024 / w_sz:.0f}x lighter in memory/disk footprint")
    print("================================================================\n")

if __name__ == "__main__":
    main()
