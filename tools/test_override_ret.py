"""tools/override_ret.py: on port/ as it is, with known bug 87's held path
put back as it was before its fix, and on made-up overrides that take each
way a path can do, or miss, its `ret`."""
import pathlib
import re

import pytest

from tools import override_ret as orr
from tools.recomp.manual_scan import scan

REPO = pathlib.Path(__file__).resolve().parents[1]
PORT = sorted((REPO / "port").glob("*.c"))
MANUAL = REPO / "port" / "recomp_manual.c"

# What the check can't follow in port/ today, each read by eye instead.
# sub_00042EE0, random graffiti's draw of a spot, draws each of its passes
# but the last through guest_call_cdecl, which takes its own return address
# off, and the last with the game's body, which does the override's `ret`:
# once on every path, read on 2026-11-28 (b3 row 34). A count picks the
# pass, which the check doesn't follow.
CANT_FOLLOW = {"sub_00042EE0": "can't be followed: a `ret` inside a loop that can come round again"}

# The XDK's pad functions answered by port/pad.c (test-integrity review,
# 2026-11-28: tests/rumble links pad.c and doesn't run these). Each is
# stdcall and pops its return address and its arguments.
PAD_ENTRY_POINTS = {
    "sub_001BD5FF": ("XGetDevices(type)", 8),
    "sub_001BD621": ("XGetDeviceChanges(type, &ins, &rem)", 16),
    "sub_001C3BA1": ("XInputOpen(type, port, slot, polling)", 20),
    "sub_001C3C16": ("XInputClose(h)", 8),
    "sub_001C3C22": ("XInputGetCapabilities(h, caps)", 12),
    "sub_001C3E14": ("XInputGetState(h, state)", 12),
    "sub_001C3E85": ("XInputSetState(h, feedback)", 12),
    "sub_001C3EBD": ("XInputPoll(h)", 8),
}


def rows_of(paths):
    return [row for rows in orr.check(paths).values() for row in rows]


def findings(paths):
    """{override: [what's wrong, without its line]}."""
    out = {}
    for name, _, _, wrong in rows_of(paths):
        for _, what in wrong:
            out.setdefault(name, []).append(re.sub(r" at line \d+| line \d+", "", what))
    return out


@pytest.fixture(scope="module")
def port_rows():
    return rows_of(PORT)


def test_every_override_in_port_does_its_ret(port_rows):
    found = {}
    for name, _, _, wrong in port_rows:
        for _, what in wrong:
            found.setdefault(name, []).append(what)
    expected = {name: [what] for name, what in CANT_FOLLOW.items()}
    assert found == expected, "\n".join(f"{n}: {w}" for n, ws in sorted(found.items()) for w in ws)


def test_every_function_the_recompiler_leaves_to_port_is_read(port_rows):
    skip, wrap, _ = scan(str(MANUAL))
    read = {int(name[4:], 16) for name, line, _, _ in port_rows if line}
    assert skip | wrap, "manual_scan found no overrides"
    assert (skip | wrap) <= read, sorted(f"{va:08X}" for va in (skip | wrap) - read)
    # MENU_NAV_WRAP's, which a macro defines, among them
    assert 0x00069040 in read and 0x0007A170 in read


def test_the_pad_entry_points_pop_their_arguments(port_rows):
    hows = {name: h for name, _, h, _ in port_rows}
    for name, (call, pops) in PAD_ENTRY_POINTS.items():
        assert hows.get(name) == [f"by hand, +{pops}"], f"{name}, {call}: {hows.get(name)}"


def test_bug_87s_held_path_without_its_ret_is_found(tmp_path):
    """The negative control: port/recomp_manual.c with the one line known
    bug 87's fix added (53f66fb) taken out again."""
    text = MANUAL.read_text(encoding="utf-8")
    start = text.index("\nvoid sub_00025740(void)\n")
    end = text.index("\n}\n", start)
    body = text[start:end]
    fix = "        g_esp += 4;   /* its `ret` */\n"
    assert body.count(fix) == 1, "sub_00025740's held path no longer reads as bug 87's fix left it"
    broken = tmp_path / "recomp_manual.c"
    broken.write_text(text[:start] + body.replace(fix, "") + text[end:], encoding="utf-8")
    rows = {name: (hows, wrong) for name, _, hows, wrong in rows_of([broken])}
    hows, wrong = rows["sub_00025740"]
    held = text[:start].count("\n") + body.replace(fix, "").splitlines().index("        return;") + 1
    assert wrong == [(held, f"the return at line {held} leaves the return address on the guest stack: "
                            "no `ret`, by the game's body, a call of the game's, or by hand")]
    assert hows == ["the game's body, or a call of the game's"]


# ---------------------------------------------------------------------------
# Made-up overrides

HEAD = """\
#include <stdint.h>
extern uint32_t g_esp, g_eax, g_ecx;
extern void *g_xbox_mem_offset;
#define GARG(n) (*(uint32_t *)((uint8_t *)g_xbox_mem_offset + g_esp + 4 * (n)))
#define SCRATCH 32u
int held(void);
"""


def made_up(tmp_path, code, name="made_up.c"):
    path = tmp_path / name
    path.write_text(HEAD + code, encoding="utf-8")
    return path


def one(tmp_path, code):
    """(how its paths do their ret, what's wrong without lines) for the one
    override in code."""
    rows = rows_of([made_up(tmp_path, code)])
    assert len(rows) == 1, rows
    _, _, hows, wrong = rows[0]
    return hows, [re.sub(r" at line \d+| line \d+", "", w) for _, w in wrong]


BODY = ["the game's body, or a call of the game's"]
BOTH = ["by hand, +4"] + BODY
NO_RET = ("the return leaves the return address on the guest stack: no `ret`, by the game's body, "
          "a call of the game's, or by hand")
NO_RET_END = ("the path to its end leaves the return address on the guest stack: no `ret`, by the "
              "game's body, a call of the game's, or by hand")


def test_the_body_and_a_pop_by_hand_each_do_it(tmp_path):
    assert one(tmp_path, """
extern void sub_00011000_gen(void);
void sub_00011000(void)
{
    if (held()) { g_esp += 4; return; }   /* its `ret` */
    sub_00011000_gen();
}
""") == (BOTH, [])


def test_bug_87s_shape_a_held_path_with_no_ret(tmp_path):
    """The held path as it was in b2.2: the letting go held, and back."""
    assert one(tmp_path, """
extern void sub_00025740_gen(void);
static int s_let_go_held;
void sub_00025740(void)
{
    if (GARG(1) == 10 && held()) {
        s_let_go_held = 1;
        return;
    }
    sub_00025740_gen();
}
""") == (BODY, [NO_RET])


def test_a_stdcall_pop_and_from_where_esp_was_kept(tmp_path):
    assert one(tmp_path, """
void sub_00012000(void)
{
    const uint32_t esp = g_esp;
    g_esp -= SCRATCH;               /* room for a guest call's outputs */
    g_eax = GARG(1);
    g_esp = esp;
    g_esp = (uint32_t)(esp + 4 + 8);   /* ret 8 */
}
""") == (["by hand, +12"], [])


def test_a_kept_esp_moved_and_given_back(tmp_path):
    assert one(tmp_path, """
void sub_00012800(void)
{
    uint32_t esp = g_esp, mask = 3;
    g_eax = mask & g_esp;
    esp += 4;                       /* its `ret` */
    g_esp = esp;
}
""") == (["by hand, +4"], [])


def test_a_second_ret_and_a_stray_pop_are_found(tmp_path):
    hows, wrong = one(tmp_path, """
extern void sub_00013000_gen(void);
void sub_00013000(void)
{
    if (held()) { sub_00013000_gen(); g_esp += 4; return; }
    if (g_eax) { sub_00013000_gen(); sub_00013000_gen(); return; }
    if (g_ecx) { g_esp += 2; return; }
    sub_00013000_gen();
}
""")
    assert hows == BODY
    assert wrong == ["the return moves g_esp +4 after the `ret` the game's code did",
                     "the return does the `ret` 2 times",
                     "the return leaves g_esp +2 from where it came in, not a `ret`"]


def test_different_pops_by_hand_are_found(tmp_path):
    assert one(tmp_path, """
void sub_00014000(void)
{
    if (held()) { g_esp += 4; return; }
    g_esp += 4 + 8;
}
""")[1] == ["its paths pop different amounts by hand (+4, +12)"]


def test_a_tail_call_and_a_pushed_call(tmp_path):
    """A call of the game's with g_esp where the override came in does its
    `ret`; one after a pushed return address pops only that."""
    assert one(tmp_path, """
extern void sub_00015010(void);
extern void sub_00015020(void);
void sub_00015000(void)
{
    uint8_t *m = (uint8_t *)g_xbox_mem_offset;
    if (held()) { sub_00015010(); return; }
    g_esp -= 4; *(uint32_t *)(m + g_esp) = 0;   /* the callee's ret pops it */
    sub_00015020();
    if (g_eax) return;
    g_esp += 4;
}
""") == (BOTH, [NO_RET])


def test_helpers_given_the_body_and_their_values(tmp_path):
    """The body passed to a helper, through a pointer to another, and a
    helper's value: 1 where it ran the body, and the esp it gives back."""
    hows, wrong = one(tmp_path, """
extern void sub_00016000_gen(void);
static void run(void (*body)(void)) { body(); }
static void pass_on(void (*gen)(void)) { if (held()) run(gen); else (*gen)(); }
static int maybe(void) { if (!held()) return 0; sub_00016000_gen(); return 1; }
static uint32_t popped(uint32_t esp) { return esp + 4u; }
void sub_00016000(void)
{
    if (g_eax == 1) { pass_on(sub_00016000_gen); return; }
    if (g_eax == 2) { g_esp = popped(g_esp); return; }
    if (maybe()) return;
    run(sub_00016000_gen);
}
""")
    assert (hows, wrong) == (BOTH, [])


def test_a_helper_that_leaves_out_the_body(tmp_path):
    assert one(tmp_path, """
extern void sub_00017000_gen(void);
static void sometimes(void (*body)(void)) { if (held()) return; body(); }
void sub_00017000(void)
{
    sometimes(sub_00017000_gen);
}
""") == (BODY, [NO_RET_END])


def test_branches_switches_and_maybes(tmp_path):
    hows, wrong = one(tmp_path, """
extern void sub_00018000_gen(void);
void sub_00018000(void)
{
    switch (g_eax) {
    case 1:
        g_esp += 4;
        return;
    case 2:
        return;
    default:
        break;
    }
    held() && (sub_00018000_gen(), 1);
}
""")
    assert (hows, wrong) == (BOTH, [NO_RET, NO_RET_END])


def test_a_ret_in_a_loop_is_listed_but_one_a_break_leaves_with_is_not(tmp_path):
    assert one(tmp_path, """
extern void sub_00019000_gen(void);
void sub_00019000(void)
{
    int i;
    for (i = 0; i < 4; i++)
        if (held()) { sub_00019000_gen(); break; }
}
""") == (BODY, [NO_RET_END])      # the break's path is followed, and the one that finds none
    assert one(tmp_path, """
extern void sub_0001A000_gen(void);
void sub_0001A000(void)
{
    int i;
    for (i = 0; i < 4; i++)
        if (i == 3) sub_0001A000_gen();
}
""")[1] == ["can't be followed: a `ret` inside a loop that can come round again"]


def test_a_push_for_each_argument_is_given_up_on_then_put_back(tmp_path):
    assert one(tmp_path, """
extern void sub_0001B000_gen(void);
static void guest_call(void (*fn)(void), int nargs, const uint32_t *args)
{
    uint32_t sv[2];
    int i;
    sv[0] = g_esp;
    for (i = nargs - 1; i >= 0; i--) { g_esp -= 4; GARG(0) = args[i]; }
    g_esp -= 4;
    fn();
    g_esp = sv[0];
}
void sub_0001B000(void)
{
    guest_call(sub_0001B000_gen, (int)g_eax, 0);
    sub_0001B000_gen();
}
""") == (BODY, [])


def test_what_isnt_followed_is_listed(tmp_path):
    assert one(tmp_path, """
void sub_0001C000(void)
{
    g_esp = (g_esp - g_eax) & ~15u;
}
""")[1] == ["the path to its end: where g_esp is can't be followed"]
    assert one(tmp_path, """
void sub_0001D000(void)
{
    g_esp++;
}
""")[1] == ["can't be followed: g_esp moved by ++ or --"]


def test_if_0_a_macro_and_another_files_helper(tmp_path):
    """An override inside #if 0 isn't one; one a macro defines is; and a
    helper another file defines, not static, is followed."""
    other = made_up(tmp_path, """
void run_body(void (*body)(void)) { body(); }
static void hidden(void (*body)(void)) { (void)body; }
""", "other.c")
    main = made_up(tmp_path, """
#if 0
void sub_0001E000(void) { }
#endif
#define WRAP(va) \\
    extern void sub_##va##_gen(void); \\
    void sub_##va(void) { run_body(sub_##va##_gen); }
WRAP(0001F000)
void hidden(void (*body)(void));
extern void sub_00020000_gen(void);
void sub_00020000(void) { hidden(sub_00020000_gen); }
""", "main.c")
    rows = orr.check([main, other])
    got = {name: (hows, [re.sub(r" at line \d+", "", w) for _, w in wrong])
           for name, _, hows, wrong in rows[str(main)]}
    assert got == {"sub_0001F000": (BODY, []), "sub_00020000": ([], [NO_RET_END])}
    assert rows[str(other)] == []


def test_the_command_line(tmp_path, capsys):
    good = made_up(tmp_path, "void sub_00021000(void) { g_esp += 4; }\n", "good.c")
    bad = made_up(tmp_path, "void sub_00022000(void) { }\n", "bad.c")
    assert orr.main([str(good), "--list"]) == 0
    out = capsys.readouterr().out
    assert "sub_00021000: by hand, +4" in out and "every path does its `ret`" in out
    assert orr.main([str(bad)]) == 1
    assert "sub_00022000: the path to its end" in capsys.readouterr().out
    assert orr.main([str(tmp_path / "none.c")]) == 2
