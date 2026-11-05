"""tools/logsum.py against the lines the engine prints.

CI never runs the game, so each line logsum reads is taken here from the
engine's own source: the printf that prints it, found by a piece of its text,
filled in with values, and read back by logsum. A line reworded, reordered or
dropped in the engine fails here, by name, before a log of it reaches
logsum.
"""
import contextlib
import io
import pathlib
import re

import pytest

from tools import logsum

ROOT = pathlib.Path(__file__).resolve().parents[1]

# A printf conversion: flags, width, precision, length, and the conversion.
CONVERSION = re.compile(r"%([-+ #0]*)(\d*)(\.\d+)?(hh|h|ll|l|L|z|j|t)?([diouxXeEfgGcsp%])")
ESCAPES = {"n": "\n", "t": "\t", '"': '"', "\\": "\\", "'": "'"}


def _literal_end(text, start):
    """The index just past the C string literal that opens at text[start]."""
    i = start + 1
    while text[i] != '"':
        i += 2 if text[i] == "\\" else 1
    return i + 1


def _unescape(body):
    return re.sub(r"\\(.)", lambda m: ESCAPES[m.group(1)], body)


def _skip_space(text, i, step):
    """Past whitespace and comments, forwards (step 1) or backwards (-1)."""
    while True:
        if step > 0 and text.startswith("/*", i):
            i = text.index("*/", i) + 2
        elif step < 0 and text.startswith("*/", i - 1):
            i = text.rindex("/*", 0, i) - 1
        elif 0 <= i < len(text) and text[i].isspace():
            i += step
        else:
            return i


def c_format(path, anchor):
    """The whole format string of the printf in path whose text holds anchor:
    its string literals joined, with any `#define NAME "..."` of the same file
    between them put in. Where several print it, they must print the same."""
    text = (ROOT / path).read_text(encoding="utf-8")
    found = {_format_at(text, m.start()) for m in re.finditer(re.escape(anchor), text)}
    assert len(found) == 1, f"{path}: {len(found) or 'no'} different lines print {anchor!r}, not 1"
    return found.pop()


def _format_at(text, where):
    # Back to the literal's opening quote, then over the literals before it.
    start = where
    while text[start] != '"' or text[start - 1] == "\\":
        start -= 1
    while True:
        i = _skip_space(text, start - 1, -1)
        if text[i] != '"':
            break
        start = i - 1
        while text[start] != '"' or text[start - 1] == "\\":
            start -= 1
    defines = dict(re.findall(r'^#define\s+(\w+)\s+"([^"\n]*)"', text, re.M))
    out, i = [], start
    while True:
        i = _skip_space(text, i, 1)
        if text[i] == '"':
            end = _literal_end(text, i)
            out.append(_unescape(text[i + 1:end - 1]))
            i = end
            continue
        name = re.match(r"\w+", text[i:])
        if not name or name.group() not in defines:
            return "".join(out)
        out.append(defines[name.group()])
        i += name.end()


def render(path, anchor, *values):
    """The line(s) the printf prints with these values, one for each of its
    conversions: a C format turned into Python's, its length modifiers
    dropped."""
    fmt = c_format(path, anchor)
    convs = [m.group(5) for m in CONVERSION.finditer(fmt) if m.group(5) != "%"]
    assert len(convs) == len(values), \
        f"{path}: {anchor!r} takes {len(convs)} values ({''.join(convs)}), the test gives {len(values)}"
    py = CONVERSION.sub(lambda m: "%" + m.group(1) + m.group(2) + (m.group(3) or "")
                        + {"u": "d", "i": "d", "p": "s"}.get(m.group(5), m.group(5)), fmt)
    return py % values


def one_line(text):
    """A printed line without its newline: logsum reads the log line by line."""
    assert text.count("\n") <= 1 and not text.rstrip("\n").count("\n"), repr(text)
    return text.rstrip("\n")


# Each line logsum reads, as the engine prints it: (file, a piece of its
# text, its values). The values are kept apart from logsum's own patterns, so
# the tests below say what logsum must make of them.
GL = "src/nv2a_gl/nv2a_gl.c"
PACE = (GL, "[PACE] %llu frames paced at the flip",
        600, 3, 21.5, 1, 2.5, 0, 10, 580, 7, 2, 1, 600)
SPIN = (GL, "; spinning the last %.2f ms", 1.25, 2.5, 4, 0.75)
HITCH = (GL, "[HITCH] frame %u: ", 1234, 41.7, 812, 3, 12.5, 6.0, ", pipelines 1.0 ms", 5, 8.25, 20.0, 2)
FENCE = ("src/kernel/nv2a_pb_exec.c", "[NV2A] waits for a fence: ", 7, 1500.0, 12.5, 3)
TMAKE = (GL, "[GL] %u textures made in ", 120, 900.0, 600.0, 300.0, 7.5, 33.0, 40, 50.0, 80, 550.0)
GL_DRAWS = (GL, "  [GL] %u draws, %u triangles", 30000, *([0] * 23), *([""] * 8), *([0] * 6))
QUEUE = ("src/kernel/xbox_memory_layout.c", "[NV2A] queue: %lu flushes", 900, 600, 64.0, 512.0, 2)
APU_LINE = ("src/apu/apu_core.c", "VOICE_ON, ", 0, 1, 0, "", "", "running", 0, 0, 5, 6, 70, 41, 29,
            3, 4, 5, 6, 7, 8)
APU = ("src/kernel/kernel_bridge.c", "  [APU] %s\\n")
CPU_HEAD = ("port/cpuprof.c", "[CPUPROF] the last %.1f s", 10.0, "")
CPU_THREAD = ("port/cpuprof.c", "[CPUPROF]   thread %d (%s) %.0f%% of a core", 4242, "jsrf_recomp", 93.0, 9100)
CPU_THREAD_BARE = ("port/cpuprof.c", "[CPUPROF]   thread %d (%s) %lu samples:", 4343, "exited", 800)
CPU_FN = ("port/cpuprof.c", "%s %s %.1f%%")
GLPROF_SHARE = (GL, "[GLPROF]   draws in all", 40.5, 12.0, 3.0, 9.5, 20.0)
GLPROF_WALK = (GL, "[GLPROF]   pushbuffer walk", 85.0, 1800)
GLPROF_FPS = (GL, "[GLPROF]   %.1f fps, ", 60.0, 60.0, 300.0, 9000.0)
CARD = (GL, "the card, by its own clock: %d frames", 590, 4.5, 9.0, 16.7, 20.0, 1, "", 0.05)
CLOCK = "src/nv2a_gl/nv2a_display_clock.c"
DISPLAY_PACED = (CLOCK, "%lu paced by it, %.2f ms into a refresh", 598, 2.0, 0.25, 1)
DISPLAY_BY_GAME = (CLOCK, '"by the game\'s clock (%s)"', "the counts don't agree")
DISPLAY = (CLOCK, "[PACE] the display: %.3f Hz")
DISPLAY_COUNTS = (600, 590, 2, 2, 1, 1, 1, 1, 2, 12, 0.5)
DISPLAY_NOT = (CLOCK, "[PACE] the display: frames paced by the game's clock",
               "RECOMP_PACE_DISPLAY=0", 12, 0.5)
# What the presenter's 10 s report names its window (ps_line's caller).
PRESENT_WINDOW = "the last 10 s"
PRESENT_HZ = (GL, "on screen for 1 refresh %llu", PRESENT_WINDOW, 590, 10, 560, 25, 3, 1, 1, 59.94)
PRESENT_NO_HZ = (GL, "on screen under 14 ms %llu", PRESENT_WINDOW, 590, 10, 5, 570, 10, 4, 1)


def apu_line():
    return render(*APU, render(*APU_LINE))


def display_line(paced):
    """The display's line in a [PACE] report, paced as the engine says it."""
    counts = DISPLAY_COUNTS
    return render(*DISPLAY, 59.998, counts[0], render(*paced), *counts[1:])


def test_pace():
    m = logsum.PACE.search(render(*PACE))
    assert m.groups() == ("600", "3", "10", "580", "7", "2", "1")


def test_pace_spin():
    m = logsum.SPIN.search(render(*PACE) + render(*SPIN))
    assert m.groups() == ("1.25", "2.50", "4", "0.75")


def test_hitch():
    m = logsum.HITCH.match(one_line(render(*HITCH)))
    assert m.groups() == ("1234", "41.7", "812", "3", "12.5", "5", "8.2", "20.0", "2")


def test_fence():
    assert logsum.FENCE.search(render(*FENCE)).groups() == ("7", "1500", "12.5", "3")


def test_textures_made():
    m = logsum.TMAKE.search(render(*TMAKE))
    assert m.groups() == ("120", "900", "600", "300", "7.50", "33.0", "40", "50", "80", "550")


def test_gl_draws():
    first = render(*GL_DRAWS).splitlines()[0]
    assert logsum.GL_DRAWS.search(first).group(1) == "30000"


def test_queue():
    assert logsum.QUEUE.search(render(*QUEUE)).groups() == ("600", "64")


def test_apu():
    assert logsum.APU.search(apu_line()).group(1) == "41"


def test_cpu_profile():
    assert logsum.CPU_HEAD.search(render(*CPU_HEAD)).group(1) == "10.0"
    line = render(*CPU_THREAD) + render(*CPU_FN, "", "sub_00191440", 41.5) + render(*CPU_FN, ",", "gl_method", 9.0)
    m = logsum.CPU_THREAD.search(line)
    assert m.groups()[:4] == ("4242", "jsrf_recomp", "93", "9100")
    bare = logsum.CPU_THREAD.search(render(*CPU_THREAD_BARE))
    assert bare.groups()[:4] == ("4343", "exited", None, "800")


def test_gl_profile():
    assert logsum.GLPROF_SHARE.search(render(*GLPROF_SHARE)).groups() == ("40.5", "9.5", "20.0")
    assert logsum.GLPROF_WALK.search(render(*GLPROF_WALK)).groups() == ("85.0", "1800")
    assert logsum.GLPROF_FPS.search(render(*GLPROF_FPS)).group(1) == "60.0"
    assert logsum.CARD.search(render(*CARD)).groups() == ("590", "4.5", "9.0")


def test_display():
    m = logsum.DISPLAY.search(display_line(DISPLAY_PACED))
    assert m.group(1, 2, 5, 6) == ("59.998", "600", "12", "0.500")
    assert m.group(4).split() == ["590", "2", "2", "1", "1", "1", "1", "2"]
    assert logsum.DISPLAY_PACED.match(m.group(3)).groups() == ("598", "2.00", "0.25", "1")
    m = logsum.DISPLAY.search(display_line(DISPLAY_BY_GAME))
    assert m.group(3) == "by the game's clock (the counts don't agree)"
    assert logsum.DISPLAY_NOT.search(render(*DISPLAY_NOT)).groups() == ("RECOMP_PACE_DISPLAY=0", "12")


def test_present_window():
    text = (ROOT / GL).read_text(encoding="utf-8")
    assert f'ps_line("{PRESENT_WINDOW}", ' in text


@pytest.mark.parametrize("site,held", [(PRESENT_HZ, (560, 25, 3, 1, 1)), (PRESENT_NO_HZ, (5, 570, 10, 4, 1))])
def test_present(site, held):
    m = logsum.PRESENT.search(render(*site))
    assert m.group(1, 2) == ("590", "10")
    assert tuple(int(x) for x in m.groups()[3:8]) == held


def pace(frames, late, held19, held30, longer):
    """A [PACE] report, the run so far: frames on time but those held."""
    on_time = frames - held19 - held30 - longer
    return render(GL, "[PACE] %llu frames paced at the flip", frames, late, 21.5, 0, 0.0, 0,
                  0, on_time, held19, held30, longer, frames) + "\n"


def linux_run():
    """A run on the Ally X, as its log has it: the start, the settings, two
    hitches, four reports (the last at exit) with what follows each."""
    rt = "src/nv2a_gl/nv2a_gfx_gl.c"
    target = " %d:%ux%u%s%s %.1f MB"
    return "".join([
        render("port/main.c", "=== JSRF-PC %s", "b1.4"),
        render("port/developer.c", "[DEV] %s=%s from ", "RECOMP_GL_PROFILE", "1"),
        render(GL, "[WIN] SDL window (%s video)", "x11", "1280x800", ""),
        render(GL, "  [GL] %s | %s | GLSL %s", "4.6 (Compatibility Profile) Mesa 25.0", "AMD Radeon", "4.60"),
        render(GL, "[GL] anti-aliasing: SMAA", "off", 4, "the settings", 8, ""),
        render(GL, "[GL] anisotropic filtering: %dx from %s", 16, "the settings", 16),
        render(rt, "[GL] %d render targets, ", 2, 0, 1, 6.0,
               render(rt, target, 1, 1280, 960, "", " +depth", 4.7) + render(rt, target, 2, 640, 480, "", " no depth", 1.2)),
        render(GL, "  [GL] DXT textures handed to the device as they are"),
        render("src/nv2a_gl/nv2a_display_clock.c", "[WIN] %s\\n",
               render("src/nv2a_gl/nv2a_display_clock.c", "the display refreshes at %.3f Hz", 59.998, "frames are paced by it")),
        render(GL, "[HITCH] frame %u: ", 100, 41.7, 812, 3, 12.5, 6.0, "", 5, 8.25, 20.0, 2),
        pace(300, 2, 1, 0, 0),
        render(*GL_DRAWS), render(*QUEUE), display_line(DISPLAY_PACED), apu_line(),
        render(GL, "[HITCH] frame %u: ", 450, 60.2, 900, 9, 30.0, 10.0, "", 12, 20.0, 10.2, 0),
        render(*CPU_HEAD), render(*CPU_THREAD), render(*CPU_FN, "", "sub_00191440", 41.5), "\n",
        render(*CPU_THREAD_BARE), render(*CPU_FN, "", "gl_method", 30.0), "\n",
        render(*GLPROF_SHARE), render(*GLPROF_WALK), render(*GLPROF_FPS), render(*CARD),
        pace(900, 50, 3, 1, 0),
        render(GL, "  [GL] %u draws, %u triangles", 90000, *([0] * 23), *([""] * 8), *([0] * 6)),
        pace(1500, 51, 4, 1, 0),
        pace(1600, 51, 4, 1, 0),
        render(*SPIN), "\n",
    ])


def windows_run():
    return "".join([
        render("port/main.c", "=== JSRF-PC %s", "b1.4"),
        render(GL, "[WIN] SDL window (%s video)", "windows", "1920x1080", ""),
        render("port/developer.c", "[DEV] Log to file off: nothing"),
        pace(600, 0, 0, 0, 0), render(*PRESENT_HZ),
        pace(1200, 10, 0, 0, 0),
        pace(1800, 10, 0, 0, 0),
    ])


def test_a_log(tmp_path):
    log = tmp_path / "last-run.log"
    log.write_text(linux_run() + windows_run(), encoding="utf-8")
    linux, windows = logsum.parse(str(log))

    assert linux["version"] == "b1.4" and linux["dev"] == ["RECOMP_GL_PROFILE=1"]
    assert linux["info"][0].startswith("[WIN] SDL window (x11 video)")
    assert any(i.startswith("[GL] 4.6") for i in linux["info"])
    assert "render scale 2x (target 1: 1280x960)" in linux["info"]
    assert "DXT textures handed to the device as they are" in linux["info"]
    assert linux["display"] == ["the display refreshes at 59.998 Hz by the kernel's count: frames are paced by it"]
    rows = linux["windows"]
    assert [r["frames"] for r in rows] == [300, 600, 600, 100]
    assert [r["late"] for r in rows] == [2, 48, 1, 0]
    assert [r["held"] for r in rows] == [1, 3, 1, 0]
    assert [r["worst"] for r in rows] == [41.7, 60.2, None, None]
    assert rows[0]["draws"] == 30000 and rows[1]["draws"] == 60000
    assert rows[0]["queue_kb"] == 600 * 64 and rows[0]["voices"] == 41
    assert rows[0]["display"].startswith("60.00 Hz: 598 of 600 paced")
    game, gpu = logsum.classify(rows[1]["window"]["cpu"])
    assert (game["tid"], gpu["tid"]) == ("4242", "4343")
    assert rows[1]["window"]["card"] == [(590, 4.5, 9.0)]
    assert linux["spin"].group(1) == "1.25"
    assert not windows["dev"] and windows["windows_os"] and windows["notes"]
    # Held on screen, at 59.94 Hz: two refreshes or more (19 ms or longer).
    assert [r["window"]["present"] for r in windows["windows"]] == [(590, 10, 25 + 3 + 1 + 1), None, None]

    out = logsum.report(str(log), [linux, windows])
    assert "| 2 | 600 | 48 | 8.0 | 3 | 1 | 60 | 100 |" in out
    assert "**Finish line (Ally X, at most 5% late and 3 held in 10 s):** 1 of 2 reports over 5% late" in out
    assert "- frame 450: 60.2 ms, 900 draws" in out
    assert "**Finish line (Windows, under 1% late):** 1 of 1 reports at 1% or more" in out


def test_no_run(tmp_path):
    log = tmp_path / "empty.log"
    log.write_text("a log from before the first start\n", encoding="utf-8")
    out = io.StringIO()
    with contextlib.redirect_stdout(out):
        assert logsum.main(["logsum.py", str(log)]) == 0
    assert "No run in this log" in out.getvalue()
