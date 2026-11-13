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
step 4; the last on up to 4 processes), checks the generated code's hash,
compiles it with Zig on every core, links it with the engine's own code from
the kit, checks the engine's hash, and puts the engine in place: written beside
the old one, then moved over it, so a failed build leaves the old engine as it
was. While it translates, Zig builds the C runtime the link needs into its
cache (WarmLink).

Everything it runs is a program in the tools or the kit, by its full path, with
an environment of its own: nothing from the system's PATH, Python or compilers.
It writes only under <root>/build (the generated code, the objects, the
pipeline's work), <root>/tools/zig-cache (Zig's cache) and the engine.

Its output, line by line, for the setup's progress bar:
  step <seconds> <seconds left> <text>   a step starts, or its estimate
                                         changes (a file compiled, or every
                                         few seconds once the step has run
                                         past it): it should take about
                                         <seconds> from now, and the whole
                                         build <seconds left>, the step's own
                                         among them
  done <engine sha256> <match|mismatch|unknown> <gen sha256> <match|mismatch|unknown>
anything else is the log. The bar goes on from where it is, never back
(launcher/setup, js_bar_move), and takes the step's part of what is left of
it, so the estimates set its speed, and a wrong one changes its speed, never
its place. Exit 0 when the engine was built, 2 when it wasn't
(its last lines say why).
"""
import argparse
import concurrent.futures
import hashlib
import heapq
import json
import os
import shutil
import stat
import subprocess
import sys
import sysconfig
import threading
import time
import zipfile

WIN = sys.platform == "win32"

# ---- The bar's estimates: the PC's times, scaled by this device's own speed
# as its steps finish.

# Each translating step's time on the Windows PC (i7-10700K), the times this
# device's are measured against: b2's three first starts there (9 November,
# test build 22a5376-b2), with tools.disasm's and tools.recomp's scaled by
# what decoding each instruction once gained them in the sandbox (110.9 to
# 56.5 s and 62.1 to 29.0 s), and tools.recomp's again by what translating
# on 4 processes gained it there (29.9 to 19.7 s).
PIPELINE_SECONDS = {
    "tools.xbe_parser": 0.2,
    "tools.disasm": 67.0,
    "tools.func_id": 1.6,
    "tools.abi_analysis": 1.3,
    "tools.recomp": 23.3,
}
# The link's, by the system the kit is for, with Zig's C runtime already
# built while translating (WarmLink): Windows' on the PC was 14.8 to 16.4 s,
# about 13 of them the runtime's build, and 0.2 s with it built (test build
# 2987e4f-speed2); Linux's on the Ally X at its usual speed 2.0 to 2.9 s,
# almost all of it the runtime's (2.8 s in the sandbox, where the engine's
# link then took 0.1 s; 0.7 s in WSL on the PC, from the Windows drive).
LINK_SECONDS = {"windows": 0.5, "linux": 0.5}
# The device's speed is measured over the steps done once they come to this
# many of the PC's seconds: xbe_parser's fraction of a second says nothing.
SPEED_MIN_SECONDS = 10.0


# Each compile at once is given 0.75 GB of the memory free. Cut at 1,000
# functions, the biggest generated file took about 0.55 GB to compile
# (recomp_0000.c, 8 MB, measured on the Windows PC's Linux VM: 534 MB at its
# peak); cut at 1 MB, the biggest take about 0.16 GB (the sandbox's: 131 and
# 159 MB), so this errs high now, and is what translate_jobs gives each
# process.
COMPILE_MB = 750
# Processes translating at once (tools.recomp --jobs, tools/recomp/workers.py):
# each loads the data itself and holds about 0.7 GB at its peak, so each is
# given a compile's share of the memory free. Past 4 the loop they share is
# under 4 s (14 s on one, in the sandbox), so more save less than 2 s.
TRANSLATE_JOBS_MAX = 4
# About how fast one compile goes with the others beside it, at the PC's
# translating speed: the compile's first estimate, divided by the device's
# speed (device_speed); from the first files done, the files' own speed. A
# compile on all cores slows more than translating on one where the power is
# limited: on the Ally X 45 to 69 KB/s in its three first starts with logs
# (b2's at its lowest TDP, b2-firststart-test2's two at its usual), on the PC
# 155 to 160 (Windows) and 97 (WSL). The Ally X's, so that there the bar errs
# slow, and speeds up once the files' own speed is known, rather than running
# ahead and crawling.
COMPILE_BYTES_PER_S = 60000
# The generated code's C files, for the compile's part of the estimates
# while translating hasn't made them yet: cut at 1 MB (tools.recomp
# --split-bytes), 44.6 MB in 46 files, the biggest 1.12 MB (the sandbox's).
GEN_FILES = 46
GEN_BYTES = 44600000
GEN_LARGEST_BYTES = 1120000
# A step past its estimate is taken to need a part OVERRUN more of what it
# has run, and its line is said again every OVERRUN_EVERY seconds, so the bar
# slows but never stops: disasm took 206 s on the Ally X at its lowest TDP,
# against the PC's 131.5, and the files compile at their own speeds, a third
# apart (the PC's first start of 3ddb1a9-bar1: recomp_0000.c 211 KB/s,
# recomp_0002.c 149 KB/s), so the files' average leaves the slowest due at
# once.
OVERRUN = 0.05
OVERRUN_EVERY = 5.0


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


def translate_jobs(asked):
    """How many processes translate at once: as many as compile_jobs would
    compile, at most TRANSLATE_JOBS_MAX."""
    jobs, why = compile_jobs(asked, TRANSLATE_JOBS_MAX)
    return min(jobs, TRANSLATE_JOBS_MAX), why


def warm_link_commands(zig, info, values, tmp):
    """WarmLink's two commands: a one-line program (written to tmp) compiled to
    an object, then that object linked for the engine's target, with the
    engine link's flags and the libraries it names by -l, but none of its
    files.

    The program is linked as an object, as the engine's link links objects: a
    link that compiles a C source too has Zig build Windows' MinGW runtime
    (mingw32.lib) for other settings, and the engine's link then built its own
    again (in the sandbox, 11.3 s after a 12.9 s warm-up, against 0.1 s after
    this one; test build 2987e4f-speed1's Windows link on the PC, 9.3 s)."""
    src = os.path.join(tmp, "warm_link.c")
    with open(src, "w", encoding="utf-8", newline="\n") as f:
        f.write("int main(void) { return 0; }\n")
    win = info["system"] == "windows"
    obj = os.path.join(tmp, "warm_link" + (".obj" if win else ".o"))
    args = fill(info["link"], dict(values, gen_objs=[], out=""))
    flags = [x for i, x in enumerate(args)
             if x.startswith("-") and x != "-o" and (i == 0 or args[i - 1] != "-o")]
    out = os.path.join(tmp, "warm_link" + (".exe" if win else ""))
    return [[zig, "cc", "-target", info["target"], "-c", src, "-o", obj],
            [zig, info["link_driver"], "-target", info["target"]] + flags + [obj, "-o", out]]


class WarmLink(threading.Thread):
    """Runs warm_link_commands' commands, one after the other, as soon as it's
    made; join() waits for them, then rc and took say how they went (rc the
    first that failed).

    Zig builds the C runtime a target needs (Windows' MinGW runtime, the
    import libraries the -l flags name, compiler_rt) into its cache the first
    time it links for it: 13 s for Windows and 3 s for Linux in the sandbox,
    almost all of the PC's 12 to 16 s link. Started as translating starts, it
    is done long before the engine's link, which then finds it all in the
    cache (0.1 s). It asks for what the engine's link asks for, so the cache
    holds the same build; if it fails, the engine's link builds it as before.
    """

    def __init__(self, cmds, cwd, env):
        super().__init__(daemon=True)
        self.cmds, self.cwd, self.env = cmds, cwd, env
        self.rc, self.out, self.took = None, "", 0.0
        self.start()

    def run(self):
        for cmd in self.cmds:
            try:
                rc, out, took = run(cmd, self.cwd, self.env, False)
            except OSError as e:
                rc, out, took = -1, str(e), 0.0
            self.rc, self.out, self.took = rc, out, self.took + took
            if rc != 0:
                return


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


def device_speed(done):
    """This device's time against the PC's (2.0: it takes twice as long), from
    the steps done, each (the PC's seconds, this device's): 1 until they come
    to SPEED_MIN_SECONDS of the PC's."""
    pc = sum(p for p, _ in done)
    here = sum(h for _, h in done)
    return here / pc if pc >= SPEED_MIN_SECONDS and here > 0 else 1.0


def compile_per_s(done, first):
    """Bytes a second one compile goes here: the files done's, each (size,
    seconds), once they are SPEED_MIN_SECONDS of work at
    COMPILE_BYTES_PER_S; first until then."""
    size = sum(n for n, _ in done)
    took = sum(t for _, t in done)
    return size / took if size / COMPILE_BYTES_PER_S >= SPEED_MIN_SECONDS and took > 0 else first


def left_of(secs, ran):
    """Seconds a step estimated at secs still needs once it has run ran: the
    rest of secs, or, past it, OVERRUN of what it has run."""
    return max(secs - ran, ran * OVERRUN)


def compile_left(running, waiting, jobs, per_s):
    """Seconds until the compile ends, as the pool runs it, each file taking
    its size over per_s: each file running, (size, seconds it has run), ends
    when that has run (left_of, if it already has), and the files waiting
    (their sizes, in the pool's order) each start as the first compile ends."""
    ends = [left_of(size / per_s, ran) for size, ran in running]
    ends += [0.0] * max(0, jobs - len(ends))
    heapq.heapify(ends)
    for size in waiting:
        heapq.heappush(ends, heapq.heappop(ends) + size / per_s)
    return max(ends) if ends else 0.0


def say_step(secs, left, text):
    """A step line for the setup's bar (the format at the top)."""
    say("step %.1f %.1f %s" % (secs, max(secs, left), text))


class Steps:
    """The bar's step lines. step(estimate, text) says one, estimate() giving
    (seconds, seconds left) from now; once the step has run past its seconds,
    its line is said again every OVERRUN_EVERY seconds with what estimate()
    gives then, until the next step or stop()."""

    def __init__(self):
        self.lock = threading.Condition()
        self.estimate = None
        self.text = ""
        self.due = 0.0
        self.stopped = False
        self.thread = threading.Thread(target=self._watch, daemon=True)
        self.thread.start()

    def step(self, estimate, text):
        with self.lock:
            secs, left = estimate()
            say_step(secs, left, text)
            self.estimate, self.text = estimate, text
            self.due = time.monotonic() + max(secs, OVERRUN_EVERY)
            self.lock.notify()

    def stop(self):
        with self.lock:
            self.stopped = True
            self.lock.notify()
        self.thread.join()

    def _watch(self):
        with self.lock:
            while not self.stopped:
                wait = self.due - time.monotonic() if self.estimate else None
                if wait is None or wait > 0:
                    self.lock.wait(wait)
                    continue
                secs, left = self.estimate()
                say_step(secs, left, self.text)
                self.due = time.monotonic() + OVERRUN_EVERY


_saying = threading.Lock()


def say(line):
    with _saying:
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
    t_all = time.monotonic()
    values = {"kit": kit, "root": root, "gen": gen, "gen_root": gen_root}
    warming = WarmLink(warm_link_commands(zig, info, values, tmp), tmp, env)
    tjobs, twhy = translate_jobs(a.jobs)
    say("translating on %d processes (%s)" % (tjobs, twhy))
    pipeline = info["pipeline"]
    link_pc = LINK_SECONDS[info["system"]]
    # Before translating has made them, the generated files as the PC's were.
    gen_before = [GEN_LARGEST_BYTES] + [(GEN_BYTES - GEN_LARGEST_BYTES) / (GEN_FILES - 1)] * (GEN_FILES - 1)
    jobs_before = compile_jobs(a.jobs, GEN_FILES)[0]
    done_steps = []   # (the PC's seconds, this device's) for each step done
    steps = Steps()
    for i, step in enumerate(pipeline):
        module = step[0]
        if a.test_gen:
            say_step(0, 0, "Translating the game (%s)" % module)
            continue
        k = device_speed(done_steps)
        secs = PIPELINE_SECONDS[module] * k
        later = (sum(PIPELINE_SECONDS[x[0]] for x in pipeline[i + 1:]) * k
                 + compile_left([], gen_before, jobs_before, COMPILE_BYTES_PER_S / k) + link_pc * k)
        t_step = time.monotonic()

        def estimate(secs=secs, later=later, t_step=t_step):
            s = left_of(secs, time.monotonic() - t_step)
            return s, s + later
        steps.step(estimate, "Translating the game (%s)" % module)
        cmd = [exe, "-E", "-s", "-m"] + fill(step, {"xbe": "game_files/default.xbe", "gen": gen,
                                                     "jobs": str(tjobs)})
        rc, out, took = run(cmd, work, env, True)
        if rc != 0:
            fail("%s stopped (exit %d)" % (module, rc))
        say("%s: %.1f s" % (module, took))
        done_steps.append((PIPELINE_SECONDS[module], took))
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
    sizes = {n: os.path.getsize(os.path.join(gen, n)) for n in sources}
    jobs, why = compile_jobs(a.jobs, len(sources))
    flags = fill(info["compile"], values)
    k = device_speed(done_steps)
    first_per_s = COMPILE_BYTES_PER_S / k
    link_s = link_pc * k
    est = compile_left([], [sizes[n] for n in sources], jobs, first_per_s)
    say("compiling %d files (%.1f MB, the biggest %.1f MB), %d at once (%s), about %.0f s"
        % (len(sources), sum(sizes.values()) / 1e6, max(sizes.values(), default=0) / 1e6, jobs, why, est))
    say("compiling, biggest first: %s" % ", ".join("%s %d" % (n, sizes[n]) for n in sources))
    started = {}    # when each file's compile started, by the pool's threads
    finished = {}   # each file compiled: (size, seconds), by this thread with steps.lock held

    def compile_estimate():
        now = time.monotonic()
        running = [(sizes[n], now - started[n]) for n in sources if n in started and n not in finished]
        waiting = [sizes[n] for n in sources if n not in started]
        s = compile_left(running, waiting, jobs, compile_per_s(list(finished.values()), first_per_s))
        return s, s + link_s
    steps.step(compile_estimate, "Compiling (%d files, %d at once)" % (len(sources), jobs))

    def compile_one(name):
        started[name] = time.monotonic()
        o = os.path.join(objs, name[:-2] + ".o")
        cmd = [zig, "cc", "-target", info["target"]] + flags + ["-w", "-c", os.path.join(gen, name), "-o", o]
        return name, o, run(cmd, work, env, False)

    failed = []
    gen_objs = {}
    t_cc = time.monotonic()
    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as pool:
        # Each file counted as it finishes, whichever it is, and the rest
        # estimated again from the files' own speed so far.
        for fut in concurrent.futures.as_completed([pool.submit(compile_one, n) for n in sources]):
            name, o, (rc, out, took) = fut.result()
            if rc != 0:
                failed.append(name)
                say("%s failed:" % name)
                for line in out.splitlines()[-30:]:
                    say("  " + line)
            else:
                gen_objs[name] = o
                say("%s: %.1f s" % (name, took))
            with steps.lock:
                finished[name] = (sizes[name], took)
            steps.step(compile_estimate, "Compiling (%d of %d files done)" % (len(finished), len(sources)))
    if failed:
        fail("%d files didn't compile: %s" % (len(failed), ", ".join(failed)))
    say("compiling: %.1f s" % (time.monotonic() - t_cc))

    # ---- 3. Linking, and the engine put in place.
    t_link = time.monotonic()

    def link_estimate():
        s = left_of(link_s, time.monotonic() - t_link)
        return s, s
    steps.step(link_estimate, "Linking the engine")
    warming.join()   # long done, as a rule: it ran while translating
    if warming.rc == 0:
        say("Zig's C runtime, built while translating: %.1f s" % warming.took)
    else:
        say("Zig's C runtime wasn't built while translating (exit %s), so the link builds it:"
            % warming.rc)
        for line in warming.out.splitlines()[-10:]:
            say("  " + line)
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
    steps.stop()
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
