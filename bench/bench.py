#!/usr/bin/env python3
"""Benchmark MoToBuddy variants on MEDUSA circuits and BuDDy examples.

usage: bench/bench.py --medusa DIR [-r REPS] [--only PATTERN] [--cpu N]
                      name=commit[@MEDUSA_DIR] [name=commit[@MEDUSA_DIR] ...]

  --medusa DIR   a MEDUSA checkout (https://github.com/QuantumFIT/MEDUSA);
                 its benchmarks/ provide the circuits
  --work DIR     where variants are built (default: bench-work/ next to
                 the repository)
  --cpu N        core to pin runs to with taskset (default 3)
  name=commit@DIR  builds that variant's MEDUSA from DIR instead of --medusa,
                 to measure a MEDUSA-side change against the same library

Example: compare main with the current branch, 7 repetitions
  bench/bench.py --medusa ../MEDUSA -r 7 base=main new=HEAD

Each variant is a MoToBuddy commit. For each one the script builds the
BuDDy examples (Release) from a git worktree and builds MEDUSA against that
commit. Runs are interleaved (every repetition runs every workload on every
variant, in rotating order) so drift in machine load affects all variants
alike. Outputs (stdout, plus res.dot for MEDUSA) must be identical across
variants; a mismatch is reported. Results: median wall time per variant,
ratio of medians against the first variant, and geometric means per group.
"""
import argparse, hashlib, os, shutil, statistics, subprocess, sys, time, math

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MEDUSA_SRC = None   # set from --medusa
VDIR = None         # set from --work
CPU = "3"

MEDUSA = [
    "Random/53", "Random/73", "LP-Grover/20", "LP-Grover/NL_19",
    "LP-PeriodFinding/16_08_05_0", "LP-PeriodFinding/19_09_05_0",
    "LP-QuantumCounting/11_05_15_0", "RevLib/frg2_297", "Feynman/hwb12",
]
BUDDY = [
    ("bddcalc c3540", "bddcalc", ["./bddcalc", "{src}/examples/bddcalc/examples/c3540.cal"]),
    ("bddcalc c1355", "bddcalc", ["./bddcalc", "{src}/examples/bddcalc/examples/c1355.cal"]),
    ("queen 10", "queen", ["./queen", "10"]),
    ("cmilner 60", "cmilner", ["./cmilner", "60"]),
    ("milner 80", "milner", ["./milner", "80"]),
]


def sh(cmd, cwd=None):
    r = subprocess.run(cmd, cwd=cwd, shell=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    if r.returncode != 0:
        sys.exit(f"command failed in {cwd}: {cmd}\n{r.stdout[-3000:]}")
    return r.stdout


def build(name, commit):
    medusa_src = MEDUSA_SRC
    if "@" in commit:
        commit, medusa_src = commit.split("@", 1)
        medusa_src = os.path.abspath(medusa_src)
    sha = sh(f"git -C {REPO} rev-parse {commit}").strip()
    d = os.path.join(VDIR, name)
    stamp = os.path.join(d, "sha")
    sha_tag = sha + "@" + medusa_src
    if os.path.exists(stamp) and open(stamp).read() == sha_tag:
        return d
    if os.path.exists(d):
        subprocess.run(f"git -C {REPO} worktree remove --force {d}/mtb", shell=True, capture_output=True)
        shutil.rmtree(d)
    os.makedirs(d)
    sh(f"git -C {REPO} worktree add --detach {d}/mtb {sha}")
    os.makedirs(f"{d}/mtb/build")
    sh("cmake .. -DCMAKE_BUILD_TYPE=Release && make -j4 bddcalc queen cmilner milner", cwd=f"{d}/mtb/build")
    sh(f"rsync -a --exclude lib --exclude obj --exclude '/MEDUSA_*' --exclude '/test_*' --exclude .git {medusa_src}/ {d}/medusa/")
    sh(f"make init MOTOBUDDY_REPO=file://{REPO} MOTOBUDDY_COMMIT={sha}", cwd=f"{d}/medusa")
    sh("make -j4 buddy_doubles_f128", cwd=f"{d}/medusa")
    os.makedirs(f"{d}/run", exist_ok=True)
    open(stamp, "w").write(sha_tag)
    return d


def run_one(variant_dir, kind, w):
    if kind == "medusa":
        cwd = f"{variant_dir}/run"
        argv = [f"{variant_dir}/medusa/MEDUSA_buddy_doubles_f128", "-f", f"{MEDUSA_SRC}/benchmarks/measure/{w}.qasm"]
    else:
        _, sub, argv = w
        cwd = f"{variant_dir}/mtb/build/examples/{sub}"
        argv = [a.replace("{src}", f"{variant_dir}/mtb") for a in argv]
    t0 = time.perf_counter()
    r = subprocess.run(["taskset", "-c", CPU] + argv, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
    dt = time.perf_counter() - t0
    # Garbage-collection lines carry timings; drop them.
    h = hashlib.sha256(b"\n".join(l for l in r.stdout.split(b"\n") if not l.startswith(b"Garbage collection") and not l.startswith(b"Nodes      =")))
    if kind == "medusa":
        h.update(open(os.path.join(cwd, "res.dot"), "rb").read())
    return dt, h.hexdigest(), r.returncode


def main():
    global MEDUSA_SRC, VDIR, CPU
    ap = argparse.ArgumentParser()
    ap.add_argument("--medusa", required=True)
    ap.add_argument("--work", default=os.path.join(os.path.dirname(REPO), "bench-work"))
    ap.add_argument("--cpu", default="3")
    ap.add_argument("-r", "--reps", type=int, default=5)
    ap.add_argument("--only", default="")
    ap.add_argument("variants", nargs="+")
    a = ap.parse_args()
    MEDUSA_SRC = os.path.abspath(a.medusa)
    VDIR = os.path.abspath(a.work)
    CPU = a.cpu
    names, dirs = [], []
    for v in a.variants:
        n, c = v.split("=", 1)
        names.append(n)
        dirs.append(build(n, c))
        print(f"built {n} ({c})", flush=True)

    work = [("medusa", w, w) for w in MEDUSA] + [("buddy", w, w[0]) for w in BUDDY]
    work = [w for w in work if a.only in w[2]]
    times = {(w[2], n): [] for w in work for n in names}
    digests = {}
    bad = set()
    for rep in range(a.reps):
        for wi, (kind, w, label) in enumerate(work):
            order = list(range(len(names)))
            k = (rep + wi) % len(order)
            order = order[k:] + order[:k]
            for i in order:
                dt, dig, rc = run_one(dirs[i], kind, w)
                times[(label, names[i])].append(dt)
                prev = digests.setdefault(label, dig)
                if dig != prev or rc != 0:
                    bad.add((label, names[i], rc))
        print(f"rep {rep + 1}/{a.reps} done", flush=True)

    base = names[0]
    col = 12
    print()
    print(f"{'workload':34s}" + "".join(f"{n:>{col}s}" for n in names) + "".join(f"{n + '/' + base:>{col + 6}s}" for n in names[1:]))
    groups = {"medusa": {n: [] for n in names[1:]}, "buddy": {n: [] for n in names[1:]}}
    for kind, w, label in work:
        med = {n: statistics.median(times[(label, n)]) for n in names}
        line = f"{label:34s}" + "".join(f"{med[n]:{col}.3f}" for n in names)
        for n in names[1:]:
            r = med[n] / med[base]
            groups[kind][n].append(r)
            line += f"{r:{col + 6}.3f}"
        print(line)
    for g, d in groups.items():
        if any(d.values()):
            print(f"{'geomean ' + g:34s}" + " " * (col * len(names)) +
                  "".join(f"{math.exp(sum(map(math.log, d[n])) / len(d[n])):{col + 6}.3f}" for n in names[1:]))
    # spread, to judge noise
    spread = max((max(times[k]) - min(times[k])) / statistics.median(times[k]) for k in times)
    print(f"\nlargest (max-min)/median over all cells: {spread:.1%}")
    if bad:
        print("\nOUTPUT MISMATCH OR NONZERO EXIT:")
        for b in sorted(bad):
            print("  ", b)
    else:
        print("all outputs identical across variants")


if __name__ == "__main__":
    main()
