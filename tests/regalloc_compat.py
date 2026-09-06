#!/usr/bin/env python3
"""Compare two compiler builds: python3 tests/regalloc_compat.py BASELINE CANDIDATE."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parent.parent


def stress_sources(directory):
    """Deterministic inputs shared by compatibility checks and benchmarks."""
    sources = {
        "calls": "int step(int x) { return x + 1; }\nint main() { int x = 0;\n"
        + "x = step(x);\n" * 600 + "return x != 600; }\n",
        "loops": "int main() { int x = 0;\n"
        "for (int a = 0; a < 2; a = a + 1) {\n"
        "for (int b = 0; b < 2; b = b + 1) {\n"
        "for (int c = 0; c < 2; c = c + 1) {\n"
        + "if (x >= 0) x = x + 1;\n" * 300
        + "}}} return x != 2400; }\n",
        "boundaries": """int identity(int x) { return x; }
int recurse(int n) {
    if (n == 0) return identity(1);
    return identity(n) + recurse(n - 1);
}
int no_calls(int x) { return (x + 1) * (x + 2); }
int main() {
    int (*fp)(int) = identity;
    int x = fp(identity(7));
    int sum = identity(1) + identity(2) + identity(3) + identity(4)
            + identity(5) + identity(6) + identity(7) + identity(8);
    for (int i = 0; i < 3; i = i + 1) {
        for (int j = 0; j < 2; j = j + 1) x = fp(x + 1);
    }
    return x != 13 || sum != 36 || recurse(5) != 16 || no_calls(3) != 20;
}
""",
    }
    paths = []
    for name, source in sources.items():
        path = directory / (name + ".cpp")
        path.write_text(source)
        paths.append(path)
    return paths


def run(command, **kwargs):
    result = subprocess.run(command, capture_output=True, timeout=60, **kwargs)
    assert result.returncode == 0, (command, result.returncode, result.stdout, result.stderr)
    return result.stdout, result.stderr


def compare(baseline, candidate):
    with tempfile.TemporaryDirectory(prefix="winds-compat-") as tmp:
        work = Path(tmp)
        # Shell suites create and delete fixtures; isolate those from the checkout.
        shutil.copytree(ROOT / "tests", work / "tests")
        shutil.copytree(ROOT / "include", work / "include")
        sources = sorted((work / "tests").glob("[0-9][0-9]_*.cpp"))
        sources += stress_sources(work)
        for level in range(3):
            for source in sources:
                assemblies, results = [], []
                for index, binary in enumerate((baseline, candidate)):
                    assembly = work / f"program{index}.s"
                    executable = work / f"program{index}.out"
                    flags = [binary, f"-O{level}", str(source)]
                    run(flags + ["-S", "-o", str(assembly)], cwd=work)
                    assemblies.append(assembly.read_bytes())
                    run(flags + ["-o", str(executable)], cwd=work)
                    results.append(run([str(executable)], cwd=work))
                assert assemblies[0] == assemblies[1], (source.name, level, "assembly changed")
                assert results[0] == results[1], (source.name, level, "program output changed")
            print(f"-O{level}: {len(sources)} identical assemblies and passing programs", flush=True)
        for script in sorted((work / "tests").glob("[0-9][0-9]_*.sh")):
            for binary in (baseline, candidate):
                run(["bash", str(script)], cwd=work, env=dict(os.environ, WINDS=binary))
            print(f"{script.name}: both builds pass", flush=True)


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    compare(*(str(Path(arg).resolve()) for arg in sys.argv[1:]))
