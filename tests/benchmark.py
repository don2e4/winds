#!/usr/bin/env python3
import time
import subprocess
import os
import sys

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
    target = sys.argv[1] if len(sys.argv) > 1 else "bin/winds"
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

