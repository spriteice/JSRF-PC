"""tools/logdigest.py on logs made up here, and its error lines against the
lines the engine prints.

Each log is written by the test, and the engine's own lines in it (the
banner, the endings, the crash and freeze reports, the error lines) are taken
from the engine's source as tools/test_logsum.py takes them, so a line
reworded in the engine fails here before a log of it reaches the digest. The
error lists themselves are checked against every line the engine prints with
an error word in it: each such line must be an error or listed as not one.
"""
import pathlib
import re

import pytest

from tools import logdigest, move_check
from tools.test_logsum import GL_DRAWS, _format_at, _literal_end, _skip_space, render

ROOT = pathlib.Path(__file__).resolve().parents[1]
MAIN = "port/main.c"
GL = "src/nv2a_gl/nv2a_gl.c"
BRIDGE = "src/kernel/kernel_bridge.c"
KFILE = "src/kernel/kernel_file.c"


def engine(path, anchor, *values):
    """The line(s) the engine prints there with these values, without their
    newlines."""
    return [line for line in render(path, anchor, *values).split("\n") if line]


BANNER = engine(MAIN, "=== JSRF-PC %s", "b2.2-test")[0]
CLEAN = engine(GL, "[GL] window closed; stopping")[0]
RESTART = engine(GL, "[WIN] restart chosen; restarting")[0]
CRASH = engine(MAIN, "[CRASH] signal %d", 11, 2, "mapped, but not with that access", 0x7F70278C0BB4)[0]
FREEZE = engine(BRIDGE, "[FROZEN] nothing has been presented", 5000, 1234)[0]
CHECK = engine("src/platform/recomp_check.h", "[CHECK] %s:%d", "freecam.c", 120, "freecam_step", "dt > 0")[0]
UNIMPL = engine("port/recomp_manual.c", "[UNIMPL] untranslated instruction", "wbinvd", 0x0019243A)[0]
ADXT = engine("port/recomp_manual.c", "[ADXT] t=%.1f", 12.5, 0x27D318, 0, 60, 0, 0, 0, 2, 0, "")[0]
SHADER_COUNT = next(line for line in engine(*GL_DRAWS) if "shader failures" in line)
SOUL = engine("port/soul_indicator.c", "the game failed the course", 25.9, 3)[0]


def failed_open(guest):
    return engine(KFILE, "FAILED errno=", guest, "/games/jsrf/x", 2, 0x48, 1, 0xC0000034)[0]


def write(tmp_path, lines, name="last-run.log", end="\n"):
    path = tmp_path / name
    path.write_bytes(end.join(lines).encode("utf-8") + end.encode("utf-8"))
    return str(path)


def filler(n, start=0):
    """Lines of a few kinds, each of its tag, that differ only by numbers."""
    shapes = ("[GL] %d draws, %d triangles, 0 clears, 1 presents",
              "  [KERNEL] NtClose: handle=0x%08X (%d)",
              "[APU] ring %d/8192 frames (%d ms)",
              "  [FILE] open D:\\Media\\Stage\\stg%04d.dat -> /games/jsrf/stg%d.dat (handle=0x1, opts=0x48)")
    return [shapes[i % len(shapes)] % (i + start, (i * 7) % 1000) for i in range(n)]


# 1. Every line accounted for.

def test_every_line_is_counted(tmp_path):
    lines = ["before the banner"] + [BANNER] + filler(500) + ["", "no tag here 12"] + [BANNER] + filler(300) + [CLEAN]
    parts, count, size = logdigest.digest_log(write(tmp_path, lines))
    assert count == len(lines)
    assert size == sum(len(line.encode()) + 1 for line in lines)
    assert sum(p.last - p.first + 1 for p in parts) == count
    for part in parts:
        assert sum(c for c, _, _ in part.tags.values()) == part.last - part.first + 1
    digest = logdigest.report([write(tmp_path, lines)])
    assert digest.count("Every line counted:") == 3
    assert "Not every line counted" not in digest


# 2. Runs split at the banner, with their endings.

def test_runs_split_at_the_banner_with_their_endings(tmp_path):
    lines = (["ERROR: ld.so: object 'x.so' from LD_PRELOAD cannot be preloaded: ignored."]
             + [BANNER] + filler(10) + [RESTART]
             + [BANNER] + filler(10) + [CLEAN, "  [PACE] 600 frames paced at the flip"]
             + [BANNER] + filler(10))
    parts, _, _ = logdigest.digest_log(write(tmp_path, lines))
    assert [(p.version, p.first, p.last) for p in parts] == [
        (None, 1, 1), ("b2.2-test", 2, 13), ("b2.2-test", 14, 26), ("b2.2-test", 27, 37)]
    assert parts[1].ending() == "a restart, chosen at line 13"
    assert parts[2].ending() == "a clean exit: the window closed at line 25, then the renderer's report"
    assert parts[3].ending() == "cut off, with no clean exit, crash report or restart before its last line"
    digest = logdigest.report([write(tmp_path, lines)])
    assert "### Before the first run: lines 1 to 1" in digest
    assert "### Run 1: b2.2-test, lines 2 to 13" in digest
    assert "### Run 3: b2.2-test, lines 27 to 37" in digest
    assert "3 runs." in digest


def test_a_log_with_no_run(tmp_path):
    digest = logdigest.report([write(tmp_path, ["just a line"])])
    assert "No run in this log" in digest
    assert "1: just a line" not in digest            # not an error
    assert "Every line counted: 1 by tag" in digest


# 3. Error, check, crash and freeze lines in full.

def test_errors_in_full_with_the_lines_around_them(tmp_path):
    lines = [BANNER] + filler(100_000)
    at = {}
    for where, line in ((20_000, CHECK), (60_000, failed_open("D:\\Media\\gone.dat")), (90_000, UNIMPL)):
        lines.insert(where, line)
        at[line] = where + 1
    digest = logdigest.report([write(tmp_path, lines)])
    for line, n in at.items():
        assert f"{n}: {line}" in digest
        assert f"{n - 1}: {lines[n - 2]}" in digest
        assert f"{n + 1}: {lines[n]}" in digest
    assert "**A check failed (RECOMP_CHECK),** 1 line:" in digest
    assert "**A file not found on the disc,** 1 line:" in digest


def test_past_100_lines_of_one_error_its_first_and_last_50(tmp_path):
    lines = [BANNER]
    for i in range(250):
        lines += [f"[GL] {i} draws, 1 triangles, 0 clears, 1 presents", UNIMPL]
    digest = logdigest.report([write(tmp_path, lines)])
    unimpl = [n for n, line in enumerate(lines, 1) if line == UNIMPL]
    assert "**An untranslated instruction reached,** 250 lines:" in digest
    assert "... 150 more of these lines; --tag lists them all ..." in digest
    for n in unimpl[:50] + unimpl[-50:]:
        assert f"\n{n}: {UNIMPL}\n" in digest
    for n in unimpl[50:-50]:
        assert f"\n{n}: {UNIMPL}\n" not in digest
    listed = logdigest.report_tag("UNIMPL", [write(tmp_path, lines)])
    assert all(f"\n{n}: {UNIMPL}\n" in listed for n in unimpl)
    assert "Its error lines, all 250:" in listed


def test_a_crash_report_is_given_whole_to_the_runs_end(tmp_path):
    report = [CRASH] + [f"    [esp+{i * 4:<4}] 0x{0x11000 + i:08X}" for i in range(300)]
    lines = [BANNER] + filler(1000) + report
    digest = logdigest.report([write(tmp_path, lines)])
    start = 1002
    assert f"**Crash report,** from line {start} to the run's end, line {len(lines)} (301 lines):" in digest
    assert f"{start - 1}: {lines[start - 2]}" in digest
    for n in list(range(start, start + 200)) + list(range(len(lines) - 19, len(lines) + 1)):
        assert f"\n{n}: {lines[n - 1]}\n" in digest
    assert "... 81 lines of the report left out ..." in digest
    assert f"\n{start + 200}: " not in digest
    assert "Ended: a crash report, from line 1002." in digest


def test_freeze_reports_their_first_and_last(tmp_path):
    lines = [BANNER] + filler(50)
    starts = []
    for k in range(3):
        starts.append(len(lines) + 1)
        lines += ["", FREEZE, "  [FROZEN] APU interrupts: 5 raised, 5 claimed by the title's ISR"]
        lines += [f"     tid {1000 + i}   ordinal 159  from 0x0018CE73  INSIDE for {k} ms  (9 calls)" for i in range(50)]
        lines += filler(20)
    digest = logdigest.report([write(tmp_path, lines)])
    first, last = starts[0] + 1, starts[2] + 1           # the freeze line, after its blank line
    assert "**Freeze report,** 3 in this run: the first and the last, each with the 40 lines after its first:" in digest
    for n in list(range(first, first + 41)) + list(range(last, last + 41)):
        assert f"\n{n}: {lines[n - 1]}\n" in digest
    assert f"\n{first + 41}: " not in digest
    assert f"\n{starts[1] + 1}: " not in digest
    assert f"a freeze report at line {first}, 3 in all" in digest


# 4. Lines that only look like errors.

def test_lines_that_only_look_like_errors(tmp_path):
    lines = [BANNER, ADXT, SHADER_COUNT, SOUL, failed_open("Z:\\Media\\Font\\jetfont.dat"),
             failed_open("U:\\99271B32E8BB\\SaveMeta.xbx")] + filler(10)
    assert "err[" in ADXT and "shader failures" in SHADER_COUNT and "failed" in SOUL
    digest = logdigest.report([write(tmp_path, lines)])
    errors = digest.split("#### Errors, checks, crashes and freezes")[1].split("####")[0]
    assert errors.strip() == "None."


# 5. The first of each kind.

def test_a_kind_is_the_line_but_its_numbers_lists_paths_and_spaces():
    kind = logdigest.kind_of
    assert kind("  [KERNEL]   ordinal 277 x30822") == kind("[KERNEL] ordinal 161   x1838")
    assert kind("[CPUPROF] sub_00191440 41.5%") == kind("[CPUPROF] sub_00012770 -3.25%")
    assert kind("[WIN] a 2560x1440 frame") == kind("[WIN] a 1920x1080 frame")
    assert kind("[DRAW] adds 6, 8, 10, 11 there") == kind("[DRAW] adds 1, 3 there")
    assert kind(failed_open("Z:\\Media\\a.dat")) == kind(failed_open("Z:\\Media\\Stage\\b.bin"))
    assert kind("[GL] 12 draws") != kind("[GL] 12 programs")
    assert kind("[OHCI0] port 1") != kind("[PAD] port 1")
    assert kind("[GL] 12 draws").startswith("[GL] ")


def test_the_first_of_each_kind_with_its_count(tmp_path):
    lines = [BANNER] + [f"[MOVER] slot {i} kind {k} of 25" for i in range(3) for k in "ABCDEFGHIJKLMNOPQRSTUVWXY"]
    digest = logdigest.report([write(tmp_path, lines)])
    assert "**[MOVER]:** 25 kinds, 75 lines" in digest
    assert "\n2 (x3): [MOVER] slot 0 kind A of 25\n" in digest
    assert "\n21 (x3): [MOVER] slot 0 kind T of 25\n" in digest
    assert "kind U of 25" not in digest
    assert "... 5 more kinds, 15 lines: --tag MOVER" in digest
    listed = logdigest.report_tag("MOVER", [write(tmp_path, lines)])
    assert "25 kinds, 75 lines." in listed
    assert "\n26 (x3): [MOVER] slot 0 kind Y of 25\n" in listed


def test_a_later_run_lists_only_its_new_kinds(tmp_path):
    lines = [BANNER, "[GL] 10 draws", "[PAD] port 1 in"] + [BANNER, "[GL] 20 draws", "[PAD] port 2 out"]
    digest = logdigest.report([write(tmp_path, lines)])
    run2 = digest.split("### Run 2")[1]
    assert "The first of each kind of line, if no earlier run of this log had it" in run2
    assert "6 (x1): [PAD] port 2 out" in run2
    assert "[GL] 20 draws" not in run2.split("#### The first")[1]
    assert "2 kinds of line an earlier run of this log had are not listed again." in run2


def test_against_an_older_log_new_and_gone(tmp_path):
    old = write(tmp_path, [BANNER, "[GL] 10 draws", "[PAD] port 1 in", "[SOUL] t=1.0 a course"], "old.log")
    new = write(tmp_path, [BANNER, "[GL] 30 draws", "[PAD] port 2 out", "[MISSION] 0910 started"], "new.log")
    digest = logdigest.report([new], against=old)
    added = digest.split("## New since")[1].split("## Gone since")[0]
    gone = digest.split("## Gone since")[1]
    assert "Tags not in" in added and "[MISSION]" in added
    assert "3 (x1): [PAD] port 2 out" in added
    assert "draws" not in added
    assert "Tags not in this build's logs: [SOUL]." in gone
    assert "3 (x1): [PAD] port 1 in" in gone
    listed = logdigest.report_tag("PAD", [new], against=old)
    assert "3 (x1) (new): [PAD] port 2 out" in listed


def test_across_logs_what_only_one_has(tmp_path):
    a = write(tmp_path, [BANNER, "[GL] 10 draws", "[PAD] port 1 in"], "pc.log")
    b = write(tmp_path, [BANNER, "[GL] 30 draws", "[WIN] SDL window (x11 video)"], "allyx.log")
    digest = logdigest.report([a, b])
    across = digest.split("## Across the logs")[1]
    only_a, only_b = across.split(f"### Only in {b}")
    assert "[PAD] port 1 in" in only_a and "[WIN]" not in only_a
    assert "[WIN] SDL window (x11 video)" in only_b and "[PAD]" not in only_b
    assert "draws" not in across


# 6. Line ends and bytes.

def test_crlf_a_last_line_without_its_end_and_bytes_that_arent_utf8(tmp_path):
    path = tmp_path / "windows.log"
    path.write_bytes(BANNER.encode() + b"\r\n[GL] 1 draws\r\n[PAD] \xff\xfe port\r\n\r\n[GL] 2 draws")
    rows = list(logdigest.read_lines(str(path)))
    assert [n for n, _, _ in rows] == [1, 2, 3, 4, 5]
    assert rows[2][1] == "[PAD] \ufffd\ufffd port"
    assert rows[3][1] == "" and rows[4][1] == "[GL] 2 draws"
    assert not any("\r" in text for _, text, _ in rows)
    assert sum(size for _, _, size in rows) == path.stat().st_size
    parts, count, _ = logdigest.digest_log(str(path))
    assert count == 5 and parts[0].tags["GL"] == [2, 2, 5]
    assert "- Last line, 5: `[GL] 2 draws`" in logdigest.report([str(path)])


def test_a_very_long_line_is_cut_and_says_so():
    assert logdigest.shown("x" * 2500) == "x" * 2000 + " ... (500 characters more)"
    assert logdigest.shown("x" * 2000) == "x" * 2000


# 7. The engine's own lines.

# A word that may make a line an error.
ERROR_WORD = re.compile(r"(?i)\b(?:fail\w*|errors?|cannot|could ?n[o']t|refus\w*|crash\w*|frozen|fatal\w*"
                        r"|corrupt\w*|bad|unable|abort\w*)\b")
PRINTS = re.compile(r"\b(?:fprintf|printf|guard_say|fputs)\s*\(")
TAG_LITERAL = re.compile(r'"\s*\[[A-Z][A-Z0-9_]*\]')
CONVERSION = re.compile(r"%[-+ #0]*(?:\*|\d*)(?:\.(?:\*|\d+))?(?:hh|h|ll|l|L|z|j|t)?([diouxXeEfgGcCsSp%])")
SAMPLE = {"%": "%", "s": "x", "S": "x", "c": "x", "C": "x", "p": "0x1",
          "f": "1.0", "e": "1.0", "E": "1.0", "g": "1.0", "G": "1.0"}


def engine_sources():
    """The engine's source files: src/ and port/, without the tests and the
    generated code."""
    for top in ("src", "port"):
        for path in sorted((ROOT / top).rglob("*")):
            rel = path.relative_to(ROOT).as_posix()
            if path.suffix in (".c", ".cpp", ".h") and not path.name.startswith("test_") \
                    and not rel.startswith("port/recomp/"):
                yield rel, path


def _call_end(text, i):
    """The index of the parenthesis that closes the call opened at text[i - 1]."""
    depth = 1
    while depth:
        if text[i] == '"':
            i = _literal_end(text, i)
            continue
        depth += {"(": 1, ")": -1}.get(text[i], 0)
        i += 1
    return i - 1


def engine_strings():
    """(file, line, text, a line's start): every string the engine prints, as
    far as the source shows it, with its adjacent strings joined. A line's
    start: the format of a call of fprintf, printf, guard_say or fputs (its
    first string), or a string that starts with a tag, wherever it is (a line
    put together with snprintf). Not: the other strings in those calls, which
    go into a line through its format's %s."""
    for rel, path in engine_sources():
        code, _ = move_check.scan(path.read_text(encoding="utf-8").split("\n"))
        text = "\n".join(code[n] for n in sorted(code))
        starts = {m.start(): True for m in TAG_LITERAL.finditer(text)}
        for m in PRINTS.finditer(text):
            end = _call_end(text, m.end())
            i, first = m.end(), True
            while i < end:
                if text[i] == '"' and text[i - 1] != "'":
                    if text[i + 1] != '"':          # "" says nothing
                        starts[i] = starts.get(i, False) or first
                    first = False
                    i = _literal_end(text, i)
                    j = _skip_space(text, i, 1)
                    while text[j] == '"':           # the same string, carried on
                        i = _literal_end(text, j)
                        j = _skip_space(text, i, 1)
                else:
                    i += 1
        seen = set()
        for start in sorted(starts):
            s = _format_at(text, start + 1)
            if (s, starts[start]) not in seen:
                seen.add((s, starts[start]))
                yield rel, text.count("\n", 0, start) + 1, s, starts[start]


def sample_lines():
    """(where, line, a line's start): each line of each engine string, its
    conversions given a value of their kind."""
    for rel, n, s, starts in engine_strings():
        for line in CONVERSION.sub(lambda m: SAMPLE.get(m.group(1), "1"), s).split("\n"):
            if line.strip():
                yield f"{rel}:{n}", line.strip(), starts


SAMPLES = list(sample_lines())


def test_the_engine_strings_are_found():
    found = {line for _, line, _ in SAMPLES}
    assert "[GL] window closed; stopping" in found
    assert any(line.startswith("[CHECK] x:1 in x: x failed:") for line in found)       # snprintf'd
    assert any(line.startswith("Failed to load default.xbe") for line in found)        # an argument
    assert not any("[CRASH] signal 10 ... fault" in line for line in found)             # a comment


def test_every_engine_line_with_an_error_word_is_classed_once():
    wrong = []
    for where, line, starts in SAMPLES:
        if not starts or not ERROR_WORD.search(line):
            continue
        error, fine = logdigest.error_group(line), logdigest.not_error(line)
        if (error is None) == (fine is None):
            wrong.append(f"{where}: {'both' if error else 'neither'}: {line}")
    assert not wrong, "class each in logdigest.ERRORS or NOT_ERRORS:\n" + "\n".join(wrong)


def test_every_error_and_not_error_is_a_line_the_engine_prints():
    rendered = {"A file not found on the disc"}         # needs a path on D: (below)
    missing = []
    for name, pattern in logdigest.ERRORS + logdigest.NOT_ERRORS:
        if name in logdigest.OUTSIDE_THE_ENGINE or name in rendered:
            continue
        if not any(re.search(pattern, line) for _, line, _ in SAMPLES):
            missing.append(name)
    assert not missing, f"no engine line matches: {missing}"


def test_a_failed_open_is_an_error_only_on_the_disc():
    assert logdigest.error_group(failed_open("D:\\Media\\Stage\\stg00.dat").strip()) == "A file not found on the disc"
    for guest in ("Z:\\Media\\Font\\jetfont.dat", "U:\\99271B32E8BB\\SaveMeta.xbx"):
        line = failed_open(guest).strip()
        assert logdigest.error_group(line) is None and logdigest.not_error(line) is not None


def test_the_banner_endings_and_reports_are_the_engines():
    assert logdigest.BANNER.match(BANNER).group(1) == "b2.2-test"
    assert CLEAN.strip() == logdigest.CLEAN_EXIT
    assert RESTART.strip() == logdigest.RESTART
    assert logdigest.CRASH_START.match(CRASH.strip())
    assert logdigest.FREEZE_START.match(FREEZE.strip())
    assert logdigest.error_group(CHECK.strip()) == "A check failed (RECOMP_CHECK)"


# 8. A million lines, read whole.

def test_the_digest_of_a_million_lines_is_under_64_kb(tmp_path):
    """A million lines, with more kinds than any log seen: 55 tags, five of
    them with 40 to 60 kinds of line and the rest with 1 to 8 (about 330
    kinds to show; real logs have 130 to 260), one kind in twenty 500
    characters long (real ones: 1 to 6 in a hundred over 300), two runs, and
    an error every 10,000 lines. Nine real logs, of 130 KB to 16 MB, give
    digests of 16 to 46 KB."""
    letters = "abcdefghijklmnopqrstuvwxyz"
    tags = [(f"TAG{t}", 40 + 5 * t if t < 5 else 1 + t % 8) for t in range(55)]
    path = tmp_path / "million.log"
    with open(path, "w", encoding="utf-8") as f:
        f.write(BANNER + "\n")
        for i in range(1, 1_000_000):
            if i == 500_000:
                f.write(RESTART + "\n" + BANNER + "\n")
            elif i % 10_000 == 0:
                f.write(UNIMPL + "\n")
            else:
                tag, kinds = tags[i % len(tags)]
                k = (i // len(tags)) % kinds
                name = letters[k % 26] + letters[k // 26]
                f.write(f"  [{tag}] t={i / 60:.2f} the {name} line: 0x{i:08X} at {i % 977}, by {i % 13}"
                        + (", and the rest of the line" * 18 if k % 20 == 19 else "") + "\n")
        f.write(CLEAN + "\n")
    digest = logdigest.report([str(path)])
    size = len(digest.encode("utf-8"))
    print(f"the digest of a million lines: {size} bytes")
    assert size < 64 * 1024, size
    assert digest.count("Every line counted:") == 2
    assert digest.count("**An untranslated instruction reached,** ") == 2          # in each run


def test_the_command(tmp_path, capsys):
    log = write(tmp_path, [BANNER, "[GL] 10 draws", "no tag", CLEAN])
    assert logdigest.main(["logdigest.py", log]) == 0
    assert capsys.readouterr().out.startswith("# JSRF-PC log digest")
    assert logdigest.main(["logdigest.py", "--tag", "none", log]) == 0
    out = capsys.readouterr().out
    assert out.startswith("# Every kind of (no tag) line") and "3 (x1): no tag" in out
    with pytest.raises(SystemExit):
        logdigest.main(["logdigest.py"])
