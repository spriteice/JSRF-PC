"""tools/logcompare.py against the lines the engine prints.

Its logs are written from the engine's own printf lines, as
tools/test_logsum.py writes them, so a line reworded in the engine fails
here as it fails there. And every checks file in tools/checks/ is held to
the engine's source: each line it names must be one the engine prints, with
a number where the engine prints one, so a check can't quietly stop
matching when a later change rewords its line.
"""
import contextlib
import functools
import io
import pathlib
import re

import pytest

from tools import logcompare, logsum
from tools.test_logdigest import CONVERSION, engine_strings
from tools.test_logsum import GL, linux_run, render, windows_run

ROOT = pathlib.Path(__file__).resolve().parents[1]
CHECKS = ROOT / "tools" / "checks"
BANNER = render("port/main.c", "=== JSRF-PC %s", "b2.2-test")
WINDOWS = render(GL, "[WIN] SDL window (%s video)", "windows", "1920x1080", "", "")
LOAD = "port/resource_load.c", "[LOAD] t=%.1f mission %08X asks for stage %08X (its file at"


def hitch(frame, ms):
    return render(GL, "[HITCH] frame %u: ", frame, ms, 800, 3, 12.5, 6.0, "", 5, 8.25, 20.0, 2)


def apu(frames):
    return render("src/apu/apu_core.c", "[APU] %d frames/s", frames, 12)


def load(stage, t=1.0):
    return render(*LOAD, t, 0x5A, stage, 0x01803000)


def write(tmp_path, text, name="last-run.log"):
    path = tmp_path / name
    path.write_text(text, encoding="utf-8")
    return str(path)


def checks_file(tmp_path, text, name="checks.ini"):
    path = tmp_path / name
    path.write_text(text, encoding="utf-8")
    return str(path)


def run(argv):
    """logcompare's exit status and what it printed."""
    out = io.StringIO()
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(out):
        status = logcompare.main(["logcompare.py", *argv])
    return status, out.getvalue()


def row(out, number):
    """A check's row of the table, as its cells."""
    line = next(s for s in out.splitlines() if s.startswith(f"| {number} |"))
    return [c.strip() for c in line.strip("|").split(" | ")]


HITCHES = """
[t]
name = The worst frame
line = [HITCH] frame {}: {ms} ms flip to flip
take = {take}
limit = <= 100
"""


# 1. Templates.

def test_a_template_takes_its_numbers_from_the_engines_line():
    t = logcompare.Template("[APU] {frames} frames/s, {} voices active")
    assert t.match(apu(1490).strip()).group("frames") == "1490"
    t = logcompare.Template("[HITCH] frame {}: {ms} ms flip to flip")
    assert t.match(hitch(450, 60.2).strip()).group("ms") == "60.2"
    t = logcompare.Template("[LOAD] t={t} mission ... asks for stage")
    assert t.match(load(0x00010002, -1.5).strip()).group("t") == "-1.5"


def test_a_template_reads_a_number_whole_and_any_run_of_spaces():
    t = logcompare.Template("thread {} {takes} takes")
    assert t.match("thread 4242     1234 takes, held").group("takes") == "1234"
    assert t.match("thread 4242 a1234 takes") is None
    t = logcompare.Template("... {ms} ms")
    assert t.match("the longest 12.75 ms").group("ms") == "12.75"
    assert t.match("x12.75 ms") is None                         # the space before it is the template's
    assert logcompare.Template("...{ms} ms").match("x12.75 ms").group("ms") == "12.75"
    t = logcompare.Template("[GL] {n} draws")
    assert t.match("[GL] 3 draws").group("n") == "3" and t.match("[GL] 3.5.1 draws") is None


@pytest.mark.parametrize("text", ["[X] {a} and {a}", "[X] {1a}", "[X] {a b}"])
def test_a_template_with_a_bad_name(text):
    with pytest.raises(logcompare.CheckError):
        logcompare.Template(text)


# 2. What a check takes.

@pytest.mark.parametrize("how,expected,where", [
    ("max", "60.2", "line 4 (max of 4)"), ("min", "25.5", "line 5 (min of 4)"),
    ("mean", "39.35", "4 values, lines 2 to 5"), ("sum", "157.4", "4 values, lines 2 to 5"),
    ("count", "4", "4 counted, lines 2 to 5"), ("first", "30", "line 2 (first of 4)"),
    ("last", "25.5", "line 5 (last of 4)"), ("p50", "30", "line 2 (p50 of 4)"),
    ("p75", "41.7", "line 3 (p75 of 4)"), ("p100", "60.2", "line 4 (p100 of 4)"),
])
def test_each_take(tmp_path, how, expected, where):
    log = write(tmp_path, BANNER + hitch(10, 30.0) + hitch(11, 41.7) + hitch(12, 60.2) + hitch(13, 25.5))
    status, out = run([checks_file(tmp_path, HITCHES.replace("{take}", how)), log])
    assert row(out, "t")[3] == expected
    assert f"after 1: {where}" in out
    assert status == (0 if float(expected) <= 100 else 1)


def test_no_line_and_a_count_of_none(tmp_path):
    log = write(tmp_path, BANNER + apu(1490))
    status, out = run([checks_file(tmp_path, HITCHES.replace("{take}", "max")), log])
    assert row(out, "t")[3:] == ["no line", "<= 100", "NO LINE"] and status == 1
    status, out = run([checks_file(tmp_path, HITCHES.replace("{take}", "count").replace("<= 100", "= 0")), log])
    assert row(out, "t")[3:] == ["0", "= 0", "PASS"] and status == 0
    status, out = run([checks_file(tmp_path, HITCHES.replace("{take}", "max")), write(tmp_path, apu(1), "x.log")])
    assert "after 1: no run in this log" in out and status == 1


def test_lines_before_the_first_banner_are_not_counted(tmp_path):
    log = write(tmp_path, hitch(1, 99.0) + BANNER + hitch(2, 30.0))
    status, out = run([checks_file(tmp_path, HITCHES.replace("{take}", "max")), log])
    assert row(out, "t")[3] == "30"


SKIPPED = """
[s]
name = The sound's frames a second, after the first three
line = [APU] {frames} frames/s, {} voices active
skip = 3
take = min
limit = >= 1480
"""


def test_skip_is_counted_in_each_run(tmp_path):
    first = BANNER + apu(10) + apu(20) + apu(30) + apu(1490) + apu(1500)
    second = BANNER + apu(5) + apu(6) + apu(7) + apu(1485)
    status, out = run([checks_file(tmp_path, SKIPPED), write(tmp_path, first + second)])
    assert row(out, "s")[3] == "1485" and status == 0
    assert "after 1: line 11 (min of 3)" in out


STAGE = """
[g]
name = The worst frame in stage 0001000A
line = [HITCH] frame {}: {ms} ms flip to flip
from = [LOAD] t={} mission ... asks for stage 0001000A
until = [LOAD] t={} mission ... asks for stage
take = max
limit = <= 50
"""


def test_from_and_until_count_one_part_of_the_run(tmp_path):
    log = write(tmp_path, BANNER + hitch(1, 90.0) + load(0x0001000A) + load(0x0001000A) + hitch(2, 45.0)
                + load(0x00010002) + hitch(3, 80.0) + load(0x0001000A) + hitch(4, 30.0))
    status, out = run([checks_file(tmp_path, STAGE), log])
    # The second line for the same stage doesn't close it; the next stage does.
    assert row(out, "g")[3] == "45" and status == 0
    assert "after 1: line 5 (max of 2)" in out


# 3. logsum's report values.

REPORTS = """
[r]
name = Every 10 s report at most 5% late
measure = report late_pct
take = max
limit = <= 5

[h]
name = And at most 3 held
measure = report held
take = max
limit = <= 3
"""


def test_the_report_keys_are_logsums_numbers(tmp_path):
    rows = [logsum.values(r) for r in logsum.parse(write(tmp_path, linux_run()))[0]["windows"]]
    for key in logcompare.REPORT_KEYS:
        assert any(isinstance(v[key], (int, float)) and not isinstance(v[key], bool) for v in rows), key


def test_a_report_value_over_the_full_reports(tmp_path):
    log = write(tmp_path, linux_run())
    full = logsum.full_reports(logsum.parse(log)[0])
    status, out = run([checks_file(tmp_path, REPORTS), log])
    # The two full reports, 48 and 1 late of 600; the first and last left out.
    assert max(logsum.values(r)["late_pct"] for r in full) == 8.0
    assert row(out, "r")[3:] == ["8", "<= 5", "FAIL"]
    assert row(out, "h")[3:] == ["3", "<= 3", "PASS"]
    assert status == 1


def test_a_report_value_counted_from_a_line(tmp_path):
    text = linux_run()
    cut = text.index(render(GL, "  [GL] %u draws, %u triangles", 90000, *([0] * 23), *([""] * 8), *([0] * 6)))
    log = write(tmp_path, text[:cut] + load(0x00010002) + text[cut:])
    status, out = run([checks_file(tmp_path, REPORTS.replace("take = max", "take = max\nfrom = [LOAD] t={} mission ...", 1)),
                       log])
    assert row(out, "r")[3] == "0.17"         # only the third report, 1 late of 600


# 4. The device.

DEVICE = """
[w]
name = Windows: under 1% late
device = windows
measure = report late_pct
take = max
limit = < 1
"""


def test_a_check_for_the_other_device(tmp_path):
    linux = write(tmp_path, linux_run(), "allyx.log")
    windows = write(tmp_path, windows_run(), "pc.log")
    status, out = run([checks_file(tmp_path, DEVICE), linux])
    assert row(out, "w")[3:] == ["-", "< 1", "not this device"] and status == 0
    status, out = run([checks_file(tmp_path, DEVICE), linux, windows])
    # The Windows run's one full report: 10 of 600 late.
    assert row(out, "w")[3:] == ["-, 1.67", "< 1", "FAIL"] and status == 1
    assert "after 1: no windows run" in out


# 5. The before and the limit.

BEFORE = """
[b]
name = The worst frame
line = [HITCH] frame {}: {ms} ms flip to flip
take = max
before = {before}
limit = {limit}
"""


@pytest.mark.parametrize("before,limit,after,cells", [
    ("65..71", "<= before", 70.0, ["65 to 71 (file)", "70", "<= 71 (before)", "PASS"]),
    ("65..71", "<= before", 72.0, ["65 to 71 (file)", "72", "<= 71 (before)", "FAIL"]),
    ("65..71", ">= before", 66.0, ["65 to 71 (file)", "66", ">= 65 (before)", "PASS"]),
    ("65..71", "< before - 1", 70.0, ["65 to 71 (file)", "70", "< 70 (before - 1)", "FAIL"]),
    ("60", "<= before * 1.1", 66.0, ["60 (file)", "66", "<= 66 (before * 1.1)", "PASS"]),
    ("60", "<= before + 5.5", 66.0, ["60 (file)", "66", "<= 65.5 (before + 5.5)", "FAIL"]),
    ("60", "<= 50", 49.0, ["60 (file)", "49", "<= 50", "PASS"]),
])
def test_a_limit_against_the_before_in_the_file(tmp_path, before, limit, after, cells):
    log = write(tmp_path, BANNER + hitch(1, after))
    status, out = run([checks_file(tmp_path, BEFORE.replace("{before}", before).replace("{limit}", limit)), log])
    assert row(out, "b")[2:] == cells
    assert status == (0 if cells[-1] == "PASS" else 1)


def test_before_logs_take_the_files_place_as_a_range(tmp_path):
    checks = checks_file(tmp_path, BEFORE.replace("{before}", "10").replace("{limit}", "<= before"))
    replays = [write(tmp_path, BANNER + hitch(1, ms), f"replay{i}.log") for i, ms in enumerate((61.0, 64.5, 63.0))]
    status, out = run([checks, write(tmp_path, BANNER + hitch(1, 64.0)), "--before", *replays])
    assert row(out, "b")[2:] == ["61 to 64.5", "64", "<= 64.5 (before)", "PASS"] and status == 0
    assert "before 2: line 2 (max of 1)" in out
    # Where the before logs have no value, the file's figure stands.
    status, out = run([checks, write(tmp_path, BANNER + hitch(1, 64.0)), "--before",
                       write(tmp_path, BANNER, "none.log")])
    assert row(out, "b")[2:] == ["10 (file)", "64", "<= 10 (before)", "FAIL"] and status == 1


def test_no_before_for_a_limit_that_needs_one(tmp_path):
    log = write(tmp_path, BANNER + hitch(1, 60.0))
    status, out = run([checks_file(tmp_path, HITCHES.replace("{take}", "max").replace("<= 100", "<= before")), log])
    assert row(out, "t")[2:] == ["-", "60", "<= before", "NO BEFORE"] and status == 1


def test_each_after_log_must_pass(tmp_path):
    good, bad = (write(tmp_path, BANNER + hitch(1, ms), f"{ms}.log") for ms in (40.0, 120.0))
    status, out = run([checks_file(tmp_path, HITCHES.replace("{take}", "max")), good, bad])
    assert row(out, "t")[3:] == ["40, 120", "<= 100", "FAIL"] and status == 1
    assert "1 failed." in out


# 6. A checks file that can't be read.

@pytest.mark.parametrize("text", [
    "[a]\nname = x\nline = [GL] {n} draws\ntake = max\nlimt = <= 1\n",                         # no limit
    "[a]\nname = x\nline = [GL] {n} draws\ntake = max\nlimit = <= 1\nlimt = 2\n",              # unknown key
    "[a]\nname = x\nline = [GL] {n} draws\ntake = most\nlimit = <= 1\n",                       # take
    "[a]\nname = x\nline = [GL] {n} draws\ntake = p0\nlimit = <= 1\n",
    "[a]\nname = x\nline = [GL] {n} draws {m}\ntake = max\nlimit = <= 1\n",                    # which number
    "[a]\nname = x\nline = [GL] {n} draws\nmeasure = m\ntake = max\nlimit = <= 1\n",
    "[a]\nname = x\nmeasure = report fps\ntake = max\nlimit = <= 1\n",                         # not logsum's
    "[a]\nname = x\nmeasure = report held\nline = [GL] {n} draws\ntake = max\nlimit = <= 1\n",
    "[a]\nname = x\ntake = max\nlimit = <= 1\n",                                               # nothing to read
    "[a]\nname = x\nline = [GL] {n} draws\ntake = max\nlimit = = before\n",                    # = takes a number
    "[a]\nname = x\nline = [GL] {n} draws\ntake = max\nlimit = about 5\n",
    "[a]\nname = x\nline = [GL] {n} draws\ntake = max\nbefore = 9..3\nlimit = <= 1\n",          # a range backwards
    "[a]\nname = x\nline = [GL] {n} draws\ntake = max\ndevice = mac\nlimit = <= 1\n",
    "[a]\nname = x\nline = [GL] {n} draws\ntake = max\nskip = -1\nlimit = <= 1\n",
    "# nothing\n",
    "name = x\n",                                                                              # no section
])
def test_a_checks_file_that_cant_be_read(tmp_path, text):
    status, out = run([checks_file(tmp_path, text), write(tmp_path, BANNER)])
    assert status == 2 and out.startswith("logcompare: ")


def test_a_log_that_isnt_there(tmp_path):
    status, out = run([checks_file(tmp_path, HITCHES.replace("{take}", "max")), str(tmp_path / "none.log")])
    assert status == 2


def test_two_wordings_of_a_line(tmp_path):
    text = HITCHES.replace("{take}", "max").replace(
        "line = [HITCH] frame {}: {ms} ms flip to flip",
        "line = [HITCH] frame {}: {ms} ms flip to flip\n  [HITCH] frame {} took {ms} ms")
    log = write(tmp_path, BANNER + hitch(1, 40.0) + "[HITCH] frame 2 took 70.5 ms\n")
    assert row(run([checks_file(tmp_path, text), log])[1], "t")[3] == "70.5"


# 7. The checks files, held to the engine's lines.

NUMERIC = "dioufeEgG"     # not %x or %p: logcompare's numbers are decimal
PRINTS_AS = {"d": r"-?\d+", "i": r"-?\d+", "o": r"\d+", "u": r"\d+", "x": r"[0-9a-f]+", "X": r"[0-9A-F]+",
             "e": r"[-+0-9.eE]+", "E": r"[-+0-9.eE]+", "f": r"-?[0-9.]+", "g": r"[-+0-9.eE]+", "G": r"[-+0-9.eE]+",
             "s": r".*", "S": r".*", "c": r".", "C": r".", "p": r"(?:0x)?[0-9a-fA-F]+"}
NUM, ANY, SPACE = "{number}", "{any}", "{space}"


def format_items(text):
    """A line of an engine format as items: its characters, and each
    conversion as (%, its letter)."""
    items, i = [], 0
    for m in CONVERSION.finditer(text):
        items += list(text[i:m.start()])
        items.append("%" if m.group(1) == "%" else ("%", m.group(1)))
        i = m.end()
    return tuple(items + list(text[i:]))


def template_items(text):
    """A template as items, read as logcompare reads it."""
    items, i = [], 0
    for m in logcompare.TOKEN.finditer(text):
        items += list(text[i:m.start()])
        items.append(ANY if m.group(0) == "..." else NUM if m.group(0).startswith("{") else SPACE)
        i = m.end()
    return tuple(items + list(text[i:]))


def fits(t, f):
    """Whether a line the format f prints can start as the template t reads:
    its fixed text as the format's, a number where it prints one, and what a
    conversion prints (a stage's %08X, say) as the template's own text."""
    @functools.lru_cache(maxsize=None)
    def go(i, j):
        if all(x == ANY for x in t[i:]):
            return True
        if j == len(f):
            return False
        a, b = t[i], f[j]
        if a == ANY:
            return go(i + 1, j) or go(i, j + 1)
        if a == SPACE:
            if b == " ":
                k = j
                while k < len(f) and f[k] == " ":
                    k += 1
                return go(i + 1, k)
            return isinstance(b, tuple) and go(i + 1, j)        # a conversion's width
        if a == NUM:
            return isinstance(b, tuple) and b[1] in NUMERIC + "sS" and go(i + 1, j + 1)
        if a == b:
            return go(i + 1, j + 1)
        if isinstance(b, tuple):
            k = i
            while k < len(t) and t[k] not in (NUM, ANY, SPACE):
                k += 1
                if re.fullmatch(PRINTS_AS[b[1]], "".join(t[i:k])) and go(k, j + 1):
                    return True
        return False
    return go(0, 0)


@functools.lru_cache(maxsize=None)
def engine_lines():
    """Each line of each string the engine prints, as format items, the
    spaces before it dropped."""
    lines = set()
    for _, _, s, _ in engine_strings():
        for line in s.split("\n"):
            if line.strip():
                lines.add(format_items(line.lstrip()))
    return lines


def printed(text):
    t = template_items(text)
    return any(fits(t, f) for f in engine_lines())


def test_a_template_is_held_to_the_engines_lines():
    assert printed("[HITCH] frame {}: {ms} ms flip to flip")
    assert printed("[LOAD] t={} mission ... asks for stage 0001000A")
    assert printed("[APU] {frames} frames/s, {} voices active")
    assert printed("thread {} {takes} takes, held {} ms")                 # [LOCK]'s, padded
    assert not printed("[HITCH] frame {}: {ms} ms flop to flip")          # reworded
    assert not printed("[HITCH] frame {}: {ms} ms flip to flip, {} drawn")
    assert not printed("[LOAD] t={} mission {} asks for stage")           # %08X is no number of ours


def test_every_checks_file_reads_lines_the_engine_prints():
    files = sorted(CHECKS.glob("*.ini"))
    assert files, "no checks file in tools/checks"
    missing = []
    for path in files:
        for check in logcompare.read_checks(str(path)):
            for t in check.lines + check.start + check.end:
                if not printed(t.text):
                    missing.append(f"{path.name} [{check.number}]: {t.text}")
    assert not missing, "no engine line reads as:\n" + "\n".join(missing)


def test_the_finish_line_on_a_log(tmp_path):
    log = write(tmp_path, linux_run() + windows_run())
    status, out = run([str(CHECKS / "finish-line.ini"), log])
    assert row(out, "1a")[3:] == ["8", "<= 5", "FAIL"]
    assert row(out, "5")[3:] == ["1.67", "< 1", "FAIL"]
    assert row(out, "3")[3:] == ["no line", "<= 50", "NO LINE"]           # no stage loaded
