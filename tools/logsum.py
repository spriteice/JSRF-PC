#!/usr/bin/env python3
"""Read JSRF-PC logs into per-10 s tables of how the game ran.

    python3 tools/logsum.py LOG [LOG ...]  > summary.md

Written for the b1.2 performance review, and kept here with
tools/test_logsum.py, which checks each line it reads against the line the
engine's source prints.

Each run in a log (a log holds one per start, restarts included) gets a
header (version, system, window, settings, Developer switches), then a row
for each periodic report, about every 10 s:

- frames, late, held: from the always-on [PACE] report. "Late" reached the
  flip after its time; "held" is flip to flip 19 ms or longer (the frame
  stayed up for two refreshes or more), and ">=30" the longer part of it.
- worst: the longest [HITCH] frame in the window, in ms. The game lists a
  frame there when it took half again the period or longer (25 ms at 60 fps;
  the first 200 of a run, then every 50th), so "<25" is none that long.
- draws and K words a frame: [GL] draws over [PACE] frames, and the walk's
  words from [GLPROF] (the GPU time profile) or, without it, the queue's
  [NV2A] report (an estimate: walks times their average size, rounded to
  the KB).
- voices: VOICE_ON from the [APU] report: the sounds the game started.
- game and GPU thread: [CPUPROF] (the CPU profile), each one's share of a
  core and its two busiest functions. The game's main thread is the one in
  sub_00191440 (D3D's KickOff) or with the most sub_ samples; the GPU thread
  the one in gl_method, nv2a_pb_scan or frame_pace. The profile keeps its own
  10 s clock, so each of its reports goes to the row whose [PACE] report is
  the fewest log lines away; a row can show none. [PRESENT] (Windows'
  presenter) is placed the same way.
- GPU thread split: [GLPROF], the share of wall time in draws, presenting,
  pacing (waiting at the flip), the rest of the pushbuffer walk (decoding the
  words and the state changes between draws), and idle (outside the walk,
  waiting for the game's next words), averaged over the window. The walk's
  own figure includes the first three, so the five add up to 100.
- card: [GLPROF]'s line for the graphics card, how long after hand-over it
  finished frames, on average and at worst, in ms.
- shown / replaced / held on screen (Windows only, a column when the log has
  [PRESENT]): what the presenter put on the display: frames shown, frames
  replaced before they could be, and frames that stayed up 19 ms or longer
  (two refreshes at 60 Hz, three at 120 Hz).
- display (Linux, a column when the log has "[PACE] the display", F6 on):
  the display's rate by the kernel's count, how many of the frames handed
  over while it kept time were paced by it, and the share of them handed
  over in the one eighth of a refresh with the most: near 100% when paced,
  spread (down to 12%) when the game's clock drifts through the refresh. Or
  why the game's clock paced. The run's header has what it said when the
  counting started or changed.

Then the run's totals, the review's finish line (Windows: under 1% late;
otherwise the Ally X's: at most 5% late and 3 held in each 10 s; the first
report, from the first frame, and the last, at exit, are shorter and left
out), and the frames over 50 ms with what [HITCH] says they spent their time
on (the finish line for entering a level is none). A value the log has no
line for is "-". Standard library only.
"""
import re
import sys

XBOX_LINES = 480        # the Xbox's frame is 480 lines at 1x, at every shape
HEADER = re.compile(r"^=== JSRF-PC (\S+)")
DEV = re.compile(r"^\[DEV\] (\S+=\S*) from developer\.cfg$")
PACE = re.compile(r"\[PACE\] (\d+) frames paced at the flip: (\d+) reached it late .*?flip to flip: "
                  r"under 14 ms (\d+), a 60th of a second (\d+), 19-30 ms (\d+), 30-45 ms (\d+), longer (\d+)")
HITCH = re.compile(r"^\[HITCH\] frame (\d+): ([0-9.]+) ms flip to flip, (\d+) draws \| (\d+) programs built "
                   r"in ([0-9.]+) ms .*?\| (\d+) textures new, ([0-9.]+) ms in textures \| ([0-9.]+) ms the rest"
                   r"(?: \| (\d+) of the new textures DXT)?")
# The whole run so far, at the end of every [PACE] report (F1 on) and in the
# renderer's report (F2 on): the last of each in a run is shown.
SPIN = re.compile(r"; spinning the last ([0-9.]+) ms \(at most ([0-9.]+), widened (\d+) times; "
                  r"a sleep ran up to ([0-9.]+) ms past the spin's start\)")
FENCE = re.compile(r"\[NV2A\] waits for a fence: (\d+) asleep, (\d+) ms in all, the longest ([0-9.]+) ms; (\d+) spun")
TMAKE = re.compile(r"\[GL\] (\d+) textures made in (\d+) ms: (\d+) ms decoding, (\d+) ms handing them to the "
                   r"device; ([0-9.]+) ms each, the longest ([0-9.]+) ms"
                   r"(?:; DXT: (\d+) as they are \((\d+) ms\), (\d+) decoded \((\d+) ms\))?")
GL_DRAWS = re.compile(r"\[GL\] (\d+) draws, \d+ triangles, \d+ clears, \d+ presents")
QUEUE = re.compile(r"\[NV2A\] queue: \d+ flushes complete at the write; (\d+) walks, (\d+) KB on average")
APU = re.compile(r"\[APU\] .*?\((\d+) VOICE_ON")
CPU_HEAD = re.compile(r"\[CPUPROF\] the last ([0-9.]+) s")
CPU_THREAD = re.compile(r"\[CPUPROF\]   thread (\d+) \(([^)]*)\) (?:(\d+)% of a core, )?(\d+) samples:(.*)")
GLPROF_SHARE = re.compile(r"\[GLPROF\]   draws in all\s+([0-9.]+)% .*?present\s+([0-9.]+)%\s+pacing\s+([0-9.]+)%")
GLPROF_WALK = re.compile(r"\[GLPROF\]   pushbuffer walk .*?:\s+([0-9.]+)% of wall time, (\d+) K words/s")
GLPROF_FPS = re.compile(r"\[GLPROF\]   ([0-9.]+) fps, ")
CARD = re.compile(r"the card, by its own clock: (\d+) frames finished (-?[0-9.]+) ms after hand-over on "
                  r"average, (-?[0-9.]+) at worst")
DISPLAY = re.compile(r"\[PACE\] the display: ([0-9.]+) Hz by the kernel's count; since the last report, "
                     r"(\d+) frames handed over while its counts kept time, (.*?); in each eighth of a "
                     r"refresh ((?:\d+ ){7}\d+); counted (\d+) times, the longest ([0-9.]+) ms")
DISPLAY_PACED = re.compile(r"^(\d+) paced by it, ([0-9.]+) ms into a refresh, ([0-9.]+) ms from that on average "
                           r"and (\d+) more than a quarter")
DISPLAY_NOT = re.compile(r"\[PACE\] the display: frames paced by the game's clock \((.*)\); counted (\d+) times")
PRESENT = re.compile(r"\[PRESENT\] the last 10 s: (\d+) frames shown, (\d+) replaced.*?on screen "
                     r"(for 1 refresh|under 14 ms) (\d+), (?:2|a 60th of a second) (\d+), (?:3|19-30 ms) (\d+), "
                     r"(?:4|30-45 ms) (\d+), (?:more|longer) (\d+)(?: \(display ([0-9.]+) Hz\))?")


def new_window():
    return {"hitch": [], "cpu": None, "share": [], "walk": [], "card": [], "present": None}


def place(run, reports, key):
    """Reports on their own 10 s clock go to the row whose [PACE] line is the
    fewest log lines away, the nearer report winning when two want one row."""
    rows = run["windows"]
    if not rows:
        return
    best = {}
    for line, value in reports:
        i = min(range(len(rows)), key=lambda k: abs(rows[k]["line"] - line))
        d = abs(rows[i]["line"] - line)
        if i not in best or d < best[i][0]:
            best[i] = (d, value)
    for i, (_, value) in best.items():
        rows[i]["window"][key] = value


def new_run(version):
    return {"version": version, "info": [], "dev": [], "windows": [], "hitches": [],
            "cur": new_window(), "last": None, "gl_draws": None, "voices": None,
            "cpu": [], "present": [], "notes": [], "windows_os": False, "display": []}


def classify(threads):
    """The game's main thread and the GPU thread, from one [CPUPROF] report."""
    game = gpu = None
    for t in threads:
        fns = t["fns"]
        if any(f in fns for f in ("gl_method", "nv2a_pb_scan", "frame_pace")):
            if gpu is None or t["samples"] > gpu["samples"]:
                gpu = t
    for t in threads:
        if t is gpu:
            continue
        if "sub_00191440" in t["fns"]:
            game = t
            break
    if game is None:
        subs = [t for t in threads if t is not gpu and any(f.startswith("sub_") for f in t["fns"])]
        game = max(subs, key=lambda t: t["samples"], default=None)
    return game, gpu


def thread_cell(t):
    if not t:
        return "-"
    core = f"{t['core']}%" if t["core"] is not None else "?"
    top = ", ".join(f"{name} {share:.0f}%" for name, share in t["top"][:2])
    return f"{core} ({top or '?'})"


def close_window(run, pace, line):
    """A periodic report: the window since the last one becomes a row."""
    frames, late, under, sixtieth, b19, b30, longer = pace
    last = run["last"] or (0, 0, 0, 0, 0, 0, 0)
    w = run["cur"]
    row = {
        "frames": frames - last[0], "late": late - last[1],
        "held": (b19 + b30 + longer) - (last[4] + last[5] + last[6]),
        "held30": (b30 + longer) - (last[5] + last[6]),
        "worst": max((h[1] for h in w["hitch"]), default=None),
        "window": w, "line": line,
    }
    run["last"] = pace
    run["windows"].append(row)
    run["cur"] = new_window()
    return row


def parse(path):
    runs = []
    run = None
    pending = None          # the row whose cumulative block is still being read
    cpu_threads = None
    with open(path, encoding="utf-8", errors="replace") as f:
        for n, raw in enumerate(f, 1):
            line = raw.rstrip("\n")
            m = HEADER.match(line)
            if m:
                run = new_run(m.group(1))
                runs.append(run)
                pending = None
                continue
            if run is None:
                continue
            s = line.strip()
            m = DEV.match(s)
            if m:
                run["dev"].append(m.group(1))
            elif s.startswith("[DEV] Log to file off") and "nothing more is written" in s:
                note = "logging was turned off for part of this run: rows are missing"
                if note not in run["notes"]:
                    run["notes"].append(note)
            elif s.startswith("[WIN] SDL window") or s.startswith("[GL] anti-aliasing:") \
                    or s.startswith("[GL] anisotropic filtering:") or re.match(r"\[GL\] \d\.\d.*\|", s):
                if len(run["info"]) < 8 and s not in run["info"]:
                    run["info"].append(s)
                if s.startswith("[WIN] SDL window (windows video)"):
                    run["windows_os"] = True
            elif s.startswith("[GL] ") and " render targets, " in s and not any(i.startswith("render scale") for i in run["info"]):
                m = re.search(r" 1:(\d+)x(\d+)", s)
                if m:
                    # From the height: a widened frame is wider than 640 times the scale.
                    run["info"].append(f"render scale {int(m.group(2)) / XBOX_LINES:g}x (target 1: {m.group(1)}x{m.group(2)})")
            if (s.startswith("[WIN] counting the display's refreshes") or s.startswith("[WIN] frames are paced by")
                    or s.startswith("[WIN] the display")) and len(run["display"]) < 12:
                run["display"].append(s[6:])
            if s.startswith("[GL] DXT textures ") and s not in run["info"]:
                run["info"].append(s[5:])
            m = SPIN.search(s)
            if m:
                run["spin"] = m
            m = TMAKE.search(s)
            if m:
                run["tmake"] = m
            m = FENCE.search(s)
            if m:
                f = run.setdefault("fence", [0, 0, 0.0, 0])
                f[0] += int(m.group(1)); f[1] += int(m.group(2))
                f[2] = max(f[2], float(m.group(3))); f[3] += int(m.group(4))
            m = HITCH.match(s)
            if m:
                h = (int(m.group(1)), float(m.group(2)), int(m.group(3)), int(m.group(4)), float(m.group(5)),
                     int(m.group(6)), float(m.group(7)), float(m.group(8)),
                     int(m.group(9)) if m.group(9) is not None else None)
                run["cur"]["hitch"].append(h)
                run["hitches"].append(h)
                continue
            m = CPU_HEAD.search(s)
            if m:
                cpu_threads = []
                run["cpu"].append((n, cpu_threads))
                continue
            m = CPU_THREAD.search(s)
            if m and cpu_threads is not None:
                # "name 12.3%" after a comma; a name can hold a comma itself:
                # "(the executable, outside its functions)" on Windows.
                tops = re.findall(r"(?:^|,) (.+?) ([0-9.]+)%(?=,|$)", m.group(5))
                cpu_threads.append({"tid": m.group(1), "name": m.group(2),
                                    "core": int(m.group(3)) if m.group(3) else None,
                                    "samples": int(m.group(4)),
                                    "fns": [t[0] for t in tops], "top": [(t[0], float(t[1])) for t in tops]})
                continue
            m = GLPROF_SHARE.search(s)
            if m:
                run["cur"]["share"].append((float(m.group(1)), float(m.group(2)), float(m.group(3))))
                continue
            m = GLPROF_WALK.search(s)
            if m:
                run["cur"]["walk"].append([float(m.group(1)), int(m.group(2)), None])
                continue
            m = GLPROF_FPS.search(s)
            if m and run["cur"]["walk"] and run["cur"]["walk"][-1][2] is None:
                run["cur"]["walk"][-1][2] = float(m.group(1))
                continue
            m = CARD.search(s)
            if m:
                run["cur"]["card"].append((int(m.group(1)), float(m.group(2)), float(m.group(3))))
                continue
            m = PRESENT.search(s)
            if m:
                # Held is up 19 ms or longer, as [PACE] counts it: by refreshes
                # when the display's rate is known (at 120 Hz a frame on time
                # stays 2), else the buckets are [PACE]'s own.
                b = [int(x) for x in m.groups()[3:8]]
                if m.group(9):
                    period = 1000.0 / float(m.group(9))
                    held = sum(c for k, c in enumerate(b) if (k + 1) * period >= 19.0)
                else:
                    held = sum(b[2:])
                run["present"].append((n, (int(m.group(1)), int(m.group(2)), held)))
                continue
            m = PACE.search(s)
            if m:
                pending = close_window(run, tuple(int(x) for x in m.groups()), n)
                continue
            # The cumulative block after [PACE]: draws, the queue's walks, voices.
            m = GL_DRAWS.search(s)
            if m and pending is not None:
                total = int(m.group(1))
                pending["draws"] = None if run["gl_draws"] is None else total - run["gl_draws"]
                if run["gl_draws"] is None:
                    pending["draws"] = total
                run["gl_draws"] = total
                continue
            m = QUEUE.search(s)
            if m and pending is not None:
                # Counted afresh for each report, not since the start.
                pending["queue_kb"] = int(m.group(1)) * int(m.group(2))
                continue
            m = DISPLAY.search(s)
            if m and pending is not None:
                eighths = [int(x) for x in m.group(4).split()]
                handed = int(m.group(2))
                top = 100.0 * max(eighths) / handed if handed else 0.0
                p = DISPLAY_PACED.match(m.group(3))
                if p:
                    cell = f"{float(m.group(1)):.2f} Hz: {p.group(1)} of {handed} paced, {top:.0f}% in one eighth"
                else:
                    why = re.sub(r"^by the game's clock \((.*)\)$", r"\1", m.group(3))
                    cell = f"{float(m.group(1)):.2f} Hz, game's clock ({why}): {top:.0f}% in one eighth"
                pending["display"] = cell
                continue
            m = DISPLAY_NOT.search(s)
            if m and pending is not None:
                pending["display"] = f"game's clock ({m.group(1)})"
                continue
            m = APU.search(s)
            if m and pending is not None:
                total = int(m.group(1))
                pending["voices"] = total - (run["voices"] or 0)
                run["voices"] = total
                continue
    for run in runs:
        place(run, run["cpu"], "cpu")
        place(run, run["present"], "present")
    return runs


def fmt(v, spec="{}", none="-"):
    return none if v is None else spec.format(v)


def report(path, runs):
    out = [f"## {path}", ""]
    if not runs:
        out += ["No run in this log (no `=== JSRF-PC` line).", ""]
    for n, run in enumerate(runs, 1):
        out.append(f"### Run {n}: {run['version']}")
        out.append("")
        for i in run["info"]:
            out.append(f"- {i}")
        out.append(f"- Developer switches: {', '.join(run['dev']) if run['dev'] else 'none'}")
        sp = run.get("spin")
        if sp:
            out.append(f"- Pacing spin at the end: the last {sp.group(1)} ms (at most {sp.group(2)}, widened "
                       f"{sp.group(3)} times; a sleep ran up to {sp.group(4)} ms past the spin's start)")
        tm = run.get("tmake")
        if tm:
            dxt = (f"; DXT {tm.group(7)} as they are ({tm.group(8)} ms), {tm.group(9)} decoded ({tm.group(10)} ms)"
                   if tm.group(7) is not None else "")
            out.append(f"- Textures made: {tm.group(1)} in {tm.group(2)} ms, {tm.group(3)} ms decoding and "
                       f"{tm.group(4)} ms handing them over; the longest {tm.group(6)} ms{dxt}")
        fw = run.get("fence")
        if fw:
            out.append(f"- Game thread's waits for a fence: {fw[0]} asleep, {fw[1] / 1000:.1f} s in all, "
                       f"the longest {fw[2]:.1f} ms; {fw[3]} spun")
        for d in run["display"]:
            out.append(f"- Display: {d}")
        for note in run["notes"]:
            out.append(f"- Note: {note}")
        out.append("")
        if not run["windows"]:
            out += ["No periodic report in this run.", ""]
            continue
        presenter = any(r["window"]["present"] for r in run["windows"])
        display = any(r.get("display") for r in run["windows"])
        out.append("| # | frames | late | late % | held | >=30 ms | worst ms | draws/frame | K words/frame | "
                   "voices | game thread | GPU thread | GPU thread: draws / present / pacing / rest of walk / "
                   "idle % | card ms avg / worst |"
                   + (" shown / replaced / held on screen |" if presenter else "")
                   + (" display |" if display else ""))
        out.append("|---" * (14 + presenter + display) + "|")
        for i, row in enumerate(run["windows"], 1):
            w = row["window"]
            frames = row["frames"]
            late_pct = 100.0 * row["late"] / frames if frames else None
            draws = row.get("draws")
            dpf = draws / frames if draws is not None and frames else None
            kwpf = None
            walks = [x for x in w["walk"] if x[2]]
            if walks:
                kwpf = sum(x[1] / x[2] for x in walks) / len(walks)
            elif row.get("queue_kb") is not None and frames:
                kwpf = row["queue_kb"] * 1024 / 4 / 1000 / frames
            game, gpu = classify(w["cpu"]) if w["cpu"] else (None, None)
            split = "-"
            if w["share"] and w["walk"]:
                d, pr, p = (sum(x[k] for x in w["share"]) / len(w["share"]) for k in range(3))
                wk = sum(x[0] for x in w["walk"]) / len(w["walk"])
                split = f"{d:.0f} / {pr:.0f} / {p:.0f} / {wk - d - pr - p:.0f} / {100.0 - wk:.0f}"
            card = "-"
            if w["card"]:
                nf = sum(c[0] for c in w["card"])
                avg = sum(c[0] * c[1] for c in w["card"]) / nf if nf else 0.0
                card = f"{avg:.1f} / {max(c[2] for c in w['card']):.1f}"
            out.append(f"| {i} | {frames} | {row['late']} | {fmt(late_pct, '{:.1f}')} | {row['held']} | "
                       f"{row['held30']} | {fmt(row['worst'], '{:.0f}', '<25')} | {fmt(dpf, '{:.0f}')} | "
                       f"{fmt(kwpf, '{:.0f}')} | {fmt(row.get('voices'))} | {thread_cell(game)} | "
                       f"{thread_cell(gpu)} | {split} | {card} |"
                       + (f" {' / '.join(map(str, w['present'])) if w['present'] else '-'} |" if presenter else "")
                       + (f" {row.get('display') or '-'} |" if display else ""))
        out.append("")
        frames = sum(r["frames"] for r in run["windows"])
        late = sum(r["late"] for r in run["windows"])
        out.append(f"**Run totals:** {frames} frames, {late} late ({100.0 * late / frames if frames else 0:.1f}%), "
                   f"{sum(r['held'] for r in run['windows'])} held, over {len(run['windows'])} reports.")
        # The finish line is per 10 s. The reports come every 10 s by the
        # clock, but the first counts from the first frame and the last is
        # the one at exit, so those two are shorter and left out. Frames are
        # no guide: a slow 10 s has few.
        full = run["windows"][1:-1]
        short = len(run["windows"]) - len(full)
        left_out = " (the first and last reports left out: shorter than 10 s)" if short else ""
        if not full:
            out.append("")
            out.append("**Finish line:** no full 10 s report in this run.")
        elif run["windows_os"]:
            over = sum(1 for r in full if r["late"] >= 0.01 * r["frames"])
            out.append("")
            out.append(f"**Finish line (Windows, under 1% late):** {over} of {len(full)} reports at 1% or "
                       f"more{left_out}.")
        else:
            over_late = sum(1 for r in full if r["late"] > 0.05 * r["frames"])
            over_held = sum(1 for r in full if r["held"] > 3)
            out.append("")
            out.append(f"**Finish line (Ally X, at most 5% late and 3 held in 10 s):** {over_late} of {len(full)} "
                       f"reports over 5% late, {over_held} with more than 3 held (the most "
                       f"{max(r['held'] for r in full)}){left_out}.")
        big = [h for h in run["hitches"] if h[1] > 50.0]
        if big:
            out.append("")
            out.append("**Frames over 50 ms** (the finish line for entering a level):")
            out.append("")
            for h in big:
                out.append(f"- frame {h[0]}: {h[1]:.1f} ms, {h[2]} draws; {h[3]} programs built in {h[4]:.1f} ms; "
                           f"{h[5]} textures new" + (f" ({h[8]} DXT as they are)" if h[8] is not None else "")
                           + f", {h[6]:.1f} ms in textures; {h[7]:.1f} ms the rest")
        out.append("")
    return "\n".join(out)


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    print("# JSRF-PC log summary")
    print()
    for path in argv[1:]:
        print(report(path, parse(path)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
