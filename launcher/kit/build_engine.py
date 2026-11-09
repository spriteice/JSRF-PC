"""Build the engine on the player's PC, from the player's own default.xbe.

Run by the launcher's setup (launcher/setup) with the tools' own Python,
never the system's:

  tools/python/python.exe -E -s kit/build_engine.py --root <launcher folder> --xbe <default.xbe>
  tools/python/bin/python3 -E -s kit/build_engine.py ...           (Linux)

  --install-wheel <wheel>  first unpacks a wheel the setup downloaded (and
                           checked) into the tools' Python, then goes on

With the kit (kit.json beside this file, made by launcher/kit/make_kit.sh)
and the tools (<root>/tools: Python with capstone and Zig), it translates the
game into C with the pipeline (the five steps of docs/building-on-linux.md,
step 4), checks the generated code's hash, compiles it with Zig on every core,
links it with the engine's own code from the kit, checks the engine's hash, and
puts the engine in place: written beside the old one, then moved over it, so a
failed build leaves the old engine as it was.

Everything it runs is a program in the tools or the kit, by its full path, with
an environment of its own: nothing from the system's PATH, Python or compilers.
It writes only under <root>/build (the generated code, the objects, the
pipeline's work), <root>/tools/zig-cache (Zig's cache) and the engine.

Its output, line by line, for the setup's progress bar:
  step <from> <to> <seconds> <text>   a step starts: the bar goes from <from>
                                      towards <to> (fractions of the build) over
                                      about <seconds>
  done <engine sha256> <match|mismatch|unknown> <gen sha256> <match|mismatch|unknown>
anything else is the log. Exit 0 when the engine was built, 2 when it wasn't
(its last lines say why).
"""
import argparse
import concurrent.futures
import hashlib
import json
import os
import shutil
import stat
import subprocess
import sys
import sysconfig
import time
import zipfile

WIN = sys.platform == "win32"

# How the build's time is shared out, from the Windows PC's first start
# (players-pc-build/FINDINGS.md: translating 210 s, compiling 45 s on 16
# threads); the bar moves by these whatever the PC.
SHARES = [
    ("tools.xbe_parser", 0.01, 2),
    ("tools.disasm", 0.48, 130),
    ("tools.func_id", 0.01, 2),
    ("tools.abi_analysis", 0.01, 2),
    ("tools.recomp", 0.29, 76),
]
COMPILE = (0.80, 0.96)
LINK = (0.96, 1.00)


# The biggest generated file takes about 0.55 GB to compile (recomp_0000.c,
# 8 MB, measured on the Windows PC's Linux VM: 534 MB at its peak), so each
# compile at once is given 0.75 GB of the memory free.
COMPILE_MB = 750
# About how fast one compile goes with the others beside it: on the Ally X,
# 8 at once, recomp_0002.c (6.3 MB) took 52.9 s, the longest, and the whole
# compile 52.9 s (9 November); this gives it 72 s.
COMPILE_BYTES_PER_S = 110000


def memory_free_mb():
    """The memory free for new programs, in MB, or 0 when it can't be read."""
    try:
        if WIN:
            import ctypes

            class MEMORYSTATUSEX(ctypes.Structure):
                _fields_ = [("dwLength", ctypes.c_ulong), ("dwMemoryLoad", ctypes.c_ulong),
                            ("ullTotalPhys", ctypes.c_ulonglong), ("ullAvailPhys", ctypes.c_ulonglong),
                            ("ullTotalPageFile", ctypes.c_ulonglong), ("ullAvailPageFile", ctypes.c_ulonglong),
                            ("ullTotalVirtual", ctypes.c_ulonglong), ("ullAvailVirtual", ctypes.c_ulonglong),
                            ("ullAvailExtendedVirtual", ctypes.c_ulonglong)]
            m = MEMORYSTATUSEX()
            m.dwLength = ctypes.sizeof(m)
            if ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(m)):
                return int(m.ullAvailPhys // (1024 * 1024))
            return 0
        with open("/proc/meminfo", encoding="ascii") as f:
            for line in f:
                if line.startswith("MemAvailable:"):
                    return int(line.split()[1]) // 1024
    except (OSError, ValueError, AttributeError):
        pass
    return 0


def compile_jobs(asked, files):
    """How many files to compile at once: every core, as many as the memory
    free allows, never more than there are files."""
    if asked:
        return max(1, asked), "as asked"
    cores = os.cpu_count() or 1
    free = memory_free_mb()
    by_mem = max(1, free // COMPILE_MB) if free else cores
    jobs = max(1, min(cores, by_mem, files))
    return jobs, "%d cores, %s free" % (cores, ("%d MB" % free) if free else "memory unknown")


def compile_estimate(sizes, jobs):
    """Seconds the compile should take, for the bar: the biggest file's time,
    or all of them shared over the compiles at once, whichever is longer."""
    if not sizes:
        return 1
    one = max(sizes) / COMPILE_BYTES_PER_S
    shared = sum(sizes) / COMPILE_BYTES_PER_S / max(1, jobs)
    return int(max(one, shared)) + 1


def say(line):
    sys.stdout.write(line + "\n")
    sys.stdout.flush()


def fail(msg):
    say("error: " + msg)
    sys.exit(2)


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        while True:
            b = f.read(1 << 20)
            if not b:
                break
            h.update(b)
    return h.hexdigest()


def gen_hash(gen):
    """As `find . -maxdepth 1 -type f -exec sha256sum {} + | LC_ALL=C sort -k2 | sha256sum`
    gives it in the generated code's folder (players-pc-build/measure)."""
    names = sorted((n for n in os.listdir(gen) if os.path.isfile(os.path.join(gen, n))),
                   key=lambda n: ("./" + n).encode("utf-8"))
    text = "".join("%s  ./%s\n" % (sha256_file(os.path.join(gen, n)), n) for n in names)
    return hashlib.sha256(text.encode("utf-8")).hexdigest()


def rmtree(path):
    def onerr(func, p, _):
        os.chmod(p, stat.S_IWRITE)
        func(p)
    if os.path.isdir(path):
        shutil.rmtree(path, onerror=onerr)


def install_wheel(wheel, site):
    """A wheel is a zip of what goes in site-packages: unpacked as it is, with
    each file's permissions (Zig's own program must stay runnable)."""
    with zipfile.ZipFile(wheel) as z:
        for info in z.infolist():
            name = info.filename
            if name.startswith("/") or ".." in name.split("/"):
                fail("%s: a file outside the wheel: %s" % (wheel, name))
            dest = os.path.join(site, *name.split("/"))
            if name.endswith("/"):
                os.makedirs(dest, exist_ok=True)
                continue
            os.makedirs(os.path.dirname(dest), exist_ok=True)
            with z.open(info) as src, open(dest, "wb") as out:
                shutil.copyfileobj(src, out, 1 << 20)
            mode = (info.external_attr >> 16) & 0o777
            if not WIN and mode:
                os.chmod(dest, mode)
    say("installed %s" % os.path.basename(wheel))


def clean_env(root, tmp):
    """The programs' whole environment: nothing of the system's but what
    Windows itself needs to start a program."""
    env = {
        "PYTHONDONTWRITEBYTECODE": "1",
        "PYTHONUTF8": "1",
        "ZIG_GLOBAL_CACHE_DIR": os.path.join(root, "tools", "zig-cache"),
        "ZIG_LOCAL_CACHE_DIR": os.path.join(root, "tools", "zig-cache"),
        "TEMP": tmp,
        "TMP": tmp,
        "TMPDIR": tmp,
        "HOME": tmp,
        # The Windows linker stamps the .exe with the time of the link unless
        # this is set, and then no two PCs build the same bytes. Zig's linker
        # takes no flag for it (--no-insert-timestamp and /Brepro are refused).
        "SOURCE_DATE_EPOCH": "1",
    }
    if WIN:
        sysroot = os.environ.get("SystemRoot") or os.environ.get("SYSTEMROOT") or r"C:\Windows"
        env.update({
            "SystemRoot": sysroot,
            "windir": sysroot,
            "PATH": os.path.join(sysroot, "System32"),
            "USERPROFILE": tmp,
            "APPDATA": tmp,
            "LOCALAPPDATA": tmp,
            "NUMBER_OF_PROCESSORS": os.environ.get("NUMBER_OF_PROCESSORS", ""),
        })
    else:
        env["PATH"] = "/nonexistent"
        env["LC_ALL"] = "C"
    return env


def run(cmd, cwd, env, log):
    t = time.monotonic()
    p = subprocess.run(cmd, cwd=cwd, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                       stdin=subprocess.DEVNULL)
    out = p.stdout.decode("utf-8", "replace")
    if log:
        for line in out.splitlines():
            say("  " + line)
    return p.returncode, out, time.monotonic() - t


def fill(args, values):
    out = []
    for a in args:
        if a == "{gen_objs}":
            out += values["gen_objs"]
            continue
        for k, v in values.items():
            if isinstance(v, str):
                a = a.replace("{%s}" % k, v)
        out.append(a)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", required=True, help="the launcher's folder")
    ap.add_argument("--xbe", help="the game's default.xbe")
    ap.add_argument("--install-wheel", action="append", default=[])
    ap.add_argument("--install-only", action="store_true", help="only the wheels")
    ap.add_argument("--jobs", type=int, default=0)
    # For a first start without the game (the setup's JSRF_SETUP_TEST_GEN):
    # generated code made up there, taken instead of translating.
    ap.add_argument("--test-gen", help=argparse.SUPPRESS)
    a = ap.parse_args()

    root = os.path.realpath(a.root)
    kit = os.path.join(root, "kit")
    tools = os.path.join(root, "tools")
    exe = os.path.realpath(sys.executable)
    if not exe.startswith(os.path.join(tools, "") ):
        fail("run with the tools' own Python (%s), not %s" % (tools, exe))
    site = sysconfig.get_paths()["purelib"]
    if not os.path.realpath(site).startswith(os.path.join(tools, "")):
        fail("the tools' Python looks for its packages outside the tools: " + site)
    for w in a.install_wheel:
        install_wheel(w, site)
    if a.install_only:
        return
    if not a.xbe:
        fail("no --xbe")

    with open(os.path.join(kit, "kit.json"), encoding="utf-8") as f:
        info = json.load(f)
    # The hashes every player's build must give, from the release's own build
    # with the game (empty: not known): kit/expected.txt, which the launcher
    # reads too.
    expected = {}
    with open(os.path.join(kit, "expected.txt"), encoding="utf-8") as f:
        for line in f:
            k, _, v = line.strip().partition("=")
            if k:
                expected[k] = v
    info["gen_sha256"] = expected.get("gen_sha256", "")
    info["engine_sha256"] = expected.get("engine_sha256", "")
    zig = os.path.join(site, "ziglang", "zig.exe" if WIN else "zig")
    if not os.path.isfile(zig):
        fail("no Zig in the tools: " + zig)
    if not os.path.isfile(a.xbe):
        fail("no default.xbe at " + a.xbe)

    build = os.path.join(root, "build")
    work = os.path.join(build, "work")
    gen_root = os.path.join(build, "generated")
    gen = os.path.join(gen_root, "recomp", "gen")
    objs = os.path.join(build, "obj")
    tmp = os.path.join(build, "tmp")
    # A new build starts clean: the last one's generated code and objects go.
    for d in (work, gen_root, objs, tmp):
        rmtree(d)
    for d in (work, gen, objs, tmp):
        os.makedirs(d)
    env = clean_env(root, tmp)
    say("kit %s for %s, Zig %s (%s)" % (info["version"], info["system"], info["zig"], zig))
    say("python %s (%s)" % (sys.version.split()[0], exe))

    # ---- 1. Translating: the pipeline in its own copy, as in the repository.
    shutil.copytree(os.path.join(kit, "pipeline"), work, dirs_exist_ok=True)
    os.makedirs(os.path.join(work, "game_files"), exist_ok=True)
    shutil.copyfile(a.xbe, os.path.join(work, "game_files", "default.xbe"))
    say("default.xbe %s" % sha256_file(os.path.join(work, "game_files", "default.xbe")))
    at = 0.0
    t_all = time.monotonic()
    for (module, share, secs), step in zip(SHARES, info["pipeline"]):
        if a.test_gen:
            say("step %.3f %.3f %d Translating the game (%s)" % (at, at + share, 0, module))
            at += share
            continue
        say("step %.3f %.3f %d Translating the game (%s)" % (at, at + share, secs, module))
        cmd = [exe, "-E", "-s", "-m"] + fill(step, {"xbe": "game_files/default.xbe", "gen": gen})
        rc, out, took = run(cmd, work, env, True)
        if rc != 0:
            fail("%s stopped (exit %d)" % (module, rc))
        say("%s: %.1f s" % (module, took))
        at += share
    if a.test_gen:
        for n in os.listdir(a.test_gen):
            shutil.copyfile(os.path.join(a.test_gen, n), os.path.join(gen, n))
    want_gen = info.get("gen_sha256", "")
    got_gen = gen_hash(gen)
    gen_state = "unknown" if not want_gen else ("match" if got_gen == want_gen else "mismatch")
    say("generated code %s (%s)" % (got_gen, gen_state))

    # ---- 2. Compiling, every core the memory allows, the biggest files first.
    sources = sorted((n for n in os.listdir(gen) if n.endswith(".c")),
                     key=lambda n: (-os.path.getsize(os.path.join(gen, n)), n))
    jobs, why = compile_jobs(a.jobs, len(sources))
    values = {"kit": kit, "root": root, "gen": gen, "gen_root": gen_root}
    flags = fill(info["compile"], values)
    est = compile_estimate([os.path.getsize(os.path.join(gen, n)) for n in sources], jobs)
    say("compiling %d files, %d at once (%s), about %d s" % (len(sources), jobs, why, est))
    say("step %.3f %.3f %d Compiling (%d files, %d at once)" % (COMPILE[0], COMPILE[1], est, len(sources), jobs))

    def compile_one(name):
        o = os.path.join(objs, name[:-2] + ".o")
        cmd = [zig, "cc", "-target", info["target"]] + flags + ["-w", "-c", os.path.join(gen, name), "-o", o]
        return name, o, run(cmd, work, env, False)

    done, failed = 0, []
    gen_objs = {}
    t_cc = time.monotonic()
    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as pool:
        # Each file counted as it finishes, whichever it is, so the bar
        # doesn't wait on the biggest.
        for fut in concurrent.futures.as_completed([pool.submit(compile_one, n) for n in sources]):
            name, o, (rc, out, took) = fut.result()
            done += 1
            if rc != 0:
                failed.append(name)
                say("%s failed:" % name)
                for line in out.splitlines()[-30:]:
                    say("  " + line)
            else:
                gen_objs[name] = o
                say("%s: %.1f s" % (name, took))
            frac = COMPILE[0] + (COMPILE[1] - COMPILE[0]) * done / max(1, len(sources))
            left = max(10, int(est - (time.monotonic() - t_cc)))
            say("step %.3f %.3f %d Compiling (%d of %d files done)" % (frac, COMPILE[1], left, done, len(sources)))
    if failed:
        fail("%d files didn't compile: %s" % (len(failed), ", ".join(failed)))
    say("compiling: %.1f s" % (time.monotonic() - t_cc))

    # ---- 3. Linking, and the engine put in place.
    say("step %.3f %.3f %d Linking the engine" % (LINK[0], LINK[1], 20))
    engine = os.path.join(root, *info["engine"].split("/"))
    os.makedirs(os.path.dirname(engine), exist_ok=True)
    new = engine + ".new"
    if os.path.exists(new):
        os.remove(new)
    # Linked in the files' own order, whatever order they compiled in, so the
    # engine's bytes don't depend on it.
    values["gen_objs"] = [gen_objs[n] for n in sorted(sources)]
    values["out"] = new
    cmd = [zig, info["link_driver"], "-target", info["target"]] + fill(info["link"], values)
    rc, out, took = run(cmd, work, env, True)
    if rc != 0 or not os.path.isfile(new):
        fail("the link stopped (exit %d)" % rc)
    say("linking: %.1f s" % took)
    got = sha256_file(new)
    want = info.get("engine_sha256", "")
    state = "unknown" if not want else ("match" if got == want else "mismatch")
    if not WIN:
        os.chmod(new, 0o755)
    try:
        os.replace(new, engine)
    except OSError as e:
        fail("can't put the engine in place (is the game running?): %s" % e)
    with open(os.path.join(build, "engine.stamp"), "w", encoding="utf-8", newline="\n") as f:
        json.dump({"engine": info["engine"], "engine_sha256": got, "engine_check": state,
                   "gen_sha256": got_gen, "gen_check": gen_state, "kit": info["version"],
                   "built": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())}, f, indent=1)
        f.write("\n")
    rmtree(tmp)
    say("engine %s (%s), %d bytes, in %.1f s" % (got, state, os.path.getsize(engine), time.monotonic() - t_all))
    say("done %s %s %s %s" % (got, state, got_gen, gen_state))


if __name__ == "__main__":
    main()
