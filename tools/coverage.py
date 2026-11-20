#!/usr/bin/env python3
"""Line and branch coverage of the core library (source/remake/core).

Build and run the instrumented tests, then summarise:

    cmake --preset wasm-coverage
    cmake --build --preset wasm-coverage
    python tools/coverage.py --run

--run deletes old counters and runs the tests through ctest first. Without
it the script reports whatever counters the last test run left behind.
--details lists the uncovered lines and the lines with a branch never taken.

The counters are gcov .gcda files, read with `llvm-cov gcov`. By default the
script uses the llvm-cov that comes with the Emscripten SDK ($EMSDK); pass
--llvm-cov to use another one. Lines can be excluded with the usual gcov
markers: GCOV_EXCL_LINE, GCOV_EXCL_START / GCOV_EXCL_STOP, and
GCOV_EXCL_BR_LINE (branches only). Use them only for code that cannot run,
with a comment saying why.
"""

import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CORE = (ROOT / "source" / "remake" / "core").resolve()


def find_llvm_cov(explicit):
    if explicit:
        return explicit
    exe = "llvm-cov.exe" if os.name == "nt" else "llvm-cov"
    emsdk = os.environ.get("EMSDK")
    if emsdk:
        candidate = Path(emsdk) / "upstream" / "bin" / exe
        if candidate.exists():
            return str(candidate)
    found = shutil.which("llvm-cov")
    if found:
        return found
    sys.exit("llvm-cov not found: set EMSDK or pass --llvm-cov")


def run_tests(build):
    for f in build.rglob("*.gcda"):
        f.unlink()
    result = subprocess.run(["ctest", "--test-dir", str(build), "--output-on-failure"])
    if result.returncode != 0:
        sys.exit("the tests failed; coverage not reported")


LINE_RE = re.compile(r"^\s*([0-9]+\*?|-|#####|=====):\s*(\d+):(.*)$")
BRANCH_RE = re.compile(r"^branch\s+(\d+)\s+(taken (\d+)|never executed)")


def collect(build, llvm_cov):
    # Read the test objects as well as the library: inline functions from the
    # core headers are often compiled into the tests, and their counts are
    # recorded there. Only core sources are reported.
    gcda = sorted(str(p) for p in build.rglob("*.gcda")
                  if "kinetics_core.dir" in p.parts or "kinetics_tests.dir" in p.parts)
    if not gcda:
        sys.exit(f"no coverage counters under {build}: build the wasm-coverage preset and use --run")
    lines = defaultdict(dict)      # source -> {line: count}
    branches = defaultdict(dict)   # source -> {(line, index): taken}
    text = defaultdict(dict)       # source -> {line: source text}
    with tempfile.TemporaryDirectory() as tmp:
        subprocess.run([llvm_cov, "gcov", "-b", "-c", "-l", *gcda], cwd=tmp,
                       stdout=subprocess.DEVNULL, check=True)
        for gcov in Path(tmp).glob("*.gcov"):
            source = None
            current = None
            seen = defaultdict(int)  # per-file branch counter for each line
            for raw in gcov.read_text(encoding="utf-8", errors="replace").splitlines():
                m = LINE_RE.match(raw)
                if m:
                    count, number, code = m.group(1), int(m.group(2)), m.group(3)
                    if number == 0:
                        if code.startswith("Source:"):
                            path = Path(code[len("Source:"):].strip())
                            if not path.is_absolute():
                                path = (build / path)
                            path = path.resolve()
                            source = path if path.is_relative_to(CORE) else None
                        continue
                    if source is None:
                        break
                    current = number
                    text[source][number] = code
                    if count != "-":
                        n = 0 if count in ("#####", "=====") else int(count.rstrip("*"))
                        lines[source][number] = lines[source].get(number, 0) + n
                    continue
                b = BRANCH_RE.match(raw)
                if b and source is not None and current is not None:
                    index = seen[current]
                    seen[current] += 1
                    taken = int(b.group(3)) if b.group(3) else 0
                    key = (current, index)
                    branches[source][key] = branches[source].get(key, 0) + taken
    return lines, branches, text


def excluded_lines(code_by_line):
    out, br_only, inside = set(), set(), False
    for number in sorted(code_by_line):
        code = code_by_line[number]
        if "GCOV_EXCL_START" in code:
            inside = True
        if inside or "GCOV_EXCL_LINE" in code:
            out.add(number)
        if "GCOV_EXCL_BR_LINE" in code:
            br_only.add(number)
        if "GCOV_EXCL_STOP" in code:
            inside = False
    return out, br_only


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--build", default=str(ROOT / "out" / "build" / "wasm-coverage"))
    ap.add_argument("--llvm-cov")
    ap.add_argument("--run", action="store_true", help="delete old counters and run the tests first")
    ap.add_argument("--details", action="store_true", help="list uncovered lines and untaken branches")
    ap.add_argument("--fail-under-lines", type=float, default=None)
    ap.add_argument("--fail-under-branches", type=float, default=None)
    args = ap.parse_args()

    build = Path(args.build).resolve()
    if args.run:
        run_tests(build)
    lines, branches, text = collect(build, find_llvm_cov(args.llvm_cov))

    rows = []
    tot = [0, 0, 0, 0]
    details = []
    for source in sorted(set(lines) | set(branches)):
        excl, br_excl = excluded_lines(text[source])
        ln = {k: v for k, v in lines[source].items() if k not in excl}
        br = {k: v for k, v in branches[source].items() if k[0] not in excl and k[0] not in br_excl}
        lc, lt = sum(1 for v in ln.values() if v > 0), len(ln)
        bc, bt = sum(1 for v in br.values() if v > 0), len(br)
        tot = [tot[0] + lc, tot[1] + lt, tot[2] + bc, tot[3] + bt]
        rows.append((source.relative_to(ROOT).as_posix(), lc, lt, bc, bt))
        missed_lines = sorted(k for k, v in ln.items() if v == 0)
        missed_br = sorted({k[0] for k, v in br.items() if v == 0})
        if missed_lines or missed_br:
            details.append((source, missed_lines, missed_br))

    def pct(a, b):
        return 100.0 * a / b if b else 100.0

    width = max(len(r[0]) for r in rows) if rows else 10
    print(f"{'file':<{width}}  {'lines':>15}  {'branches':>15}")
    for name, lc, lt, bc, bt in rows:
        print(f"{name:<{width}}  {lc:>5}/{lt:<5}{pct(lc, lt):5.1f}%  {bc:>5}/{bt:<5}{pct(bc, bt):5.1f}%")
    print(f"{'total':<{width}}  {tot[0]:>5}/{tot[1]:<5}{pct(tot[0], tot[1]):5.1f}%  "
          f"{tot[2]:>5}/{tot[3]:<5}{pct(tot[2], tot[3]):5.1f}%")

    if args.details:
        for source, ml, mb in details:
            print(f"\n{source.relative_to(ROOT).as_posix()}")
            for n in sorted(set(ml) | set(mb)):
                tag = "line  " if n in ml else "branch"
                print(f"  {tag} {n:5}: {text[source].get(n, '').strip()}")

    ok = True
    if args.fail_under_lines is not None and pct(tot[0], tot[1]) < args.fail_under_lines:
        ok = False
    if args.fail_under_branches is not None and pct(tot[2], tot[3]) < args.fail_under_branches:
        ok = False
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
