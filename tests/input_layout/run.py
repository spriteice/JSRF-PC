"""Compile the layouts (src/input/input_layout.c) and the layouts in use
(src/input/input_remap.c), and drive them without SDL.

Builds both whole, as the engine builds them, with the in-game menu's rows
(XBOXRECOMP_MENU); test_main.c keeps the input layer's fixed tables as they
were before the layouts (xinput_device.c's pad table and kb_merge, and
xbox_InputKeyboardMove's keys, over the window's twenty fixed places for
keys), as what the defaults must give, and stands in for remap.cfg (beside
the settings file, and no other), the window's own keys, the inputs' names
(xinput_device.c's, from SDL's) and the menu's xbox_MenuAdd.

The defaults: each control's inputs, the pad's at their Xbox places with
the bumpers as Black and White and its Back none of the game's, its R3 the
free camera's switch and the game's Right stick click none, and the keys
by their places, each input on one control, with the mouse's left, middle
and right buttons second on A, B and the R trigger, keypad Enter on Start
and a keyboard's Back key on Back, and none for the free camera. The pad,
with R3 given back to Right stick click: every one of the fourteen buttons
the table read, on or off together, with the triggers at their edges and
half way and the pad's Back handed over or not, gives what the table gave
with the menu there, and SDL's other buttons change nothing; as it starts,
R3 presses nothing the game sees. The keys, on the window's own layout
(the defaults until 2026-11-20): every one of its twenty places, held or
not together, by its first key, its second or both, over three pads, gives
what kb_merge gave, and says a key was held when one was. The movement keys: what
xbox_InputKeyboardMove gave for every one of the twenty. A layout's other
uses: two inputs on a control, the most of them; a trigger on Start from
half way; an input out of range, or none, pressing nothing; a key no
control uses, not held as far as the game knows; Space and the mouse's left
each pressing A alone; a mouse button and the last key. A row's bind: the
swap, a control's own input again, a second key kept, two
controls' input, and none. One of a control's places, as Keyboard layout's
rows set its two keys: the swap place by place, a row's own two swapping,
none, and an input that would be one control's twice. The inputs a layout
uses, for the latency log.
The pages: Controller layout's seventeen rows, Toggle free camera last,
and Keyboard layout's twenty-four, each with Reset to default, in one add, each Input's, left
alone by the presets, with a hint, and showing the defaults' inputs.
Binding through them: the swap, kept in remap.cfg line by line and back
to no line at a default, said in the log; none, as the pad's Back or F1
hands it; Guide, the pad's Back, the window's keys and a key with no name
refused, changing nothing; a mouse
button; Keyboard moves following the left stick's rows; and the keyboard's
rows taking two presses, the first key again or none for the second, and
none for the first, refusing as the first does. Reset to
default on each page alone. At start, the lines the rows kept read back as
they were, Toggle free camera's too, an unknown name or an input a layout
can't have keeps the default, which the log says, and the keyboard reads no
free camera's line; and spriteice's remap.cfg, kept under the old defaults,
reads as the new ones, saying nothing. The free camera's switch on the pad,
as the window hands over each button and trigger: R3 runs it once a press,
not while held, nor with the menu open, then or once it shuts; a trigger from half
way; each pad its own; bound to A, A runs it and R3 presses the game's A;
nothing for the Xbox pad's own controls or an input out of range.
Requires GCC on PATH, including MinGW on Windows.
CC, when set, is the compiler instead of gcc.
"""
import os
import pathlib
import shlex
import subprocess
import sys
import tempfile

root = pathlib.Path(__file__).resolve().parents[2]
cc = shlex.split(os.environ.get("CC", "gcc"))

with tempfile.TemporaryDirectory(prefix="input_layout-") as directory:
    exe = str(pathlib.Path(directory) / "input_layout")
    subprocess.run(cc + ["-O2", "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-DXBOXRECOMP_MENU",
                    "-I", "src", "-I", "src/input", "-I", "src/menu",
                    "src/input/input_layout.c", "src/input/input_remap.c",
                    "tests/input_layout/test_main.c", "-o", exe], cwd=root, check=True)
    # Each case, and the lines it must say in the log, in their order.
    said = {
        "rows": [],
        "bind_rows": [
            "[INPUT] controller layout: A is X", "[INPUT] controller layout: X is A",
            "[INPUT] controller layout: L trigger is None", "[INPUT] controller layout: Back is LT",
            "[INPUT] controller layout: Y is None", "[INPUT] controller layout: Y is Y",
            "[INPUT] keyboard layout: A is Space, Left Shift",
            "[INPUT] keyboard layout: B is Mouse left, Mouse middle",
            "[INPUT] keyboard layout: Y is Mouse back",
            "[INPUT] keyboard layout: Left stick up is I",
            "[INPUT] keyboard layout: Start is Keypad Enter, Return",
            "[INPUT] keyboard layout: Start is Keypad Enter",
            "[INPUT] keyboard layout: Back is Escape, Mouse forward",
            "[INPUT] keyboard layout: Back is Escape",
            "[INPUT] keyboard layout: Back is None",
        ],
        "reset": [
            "[INPUT] controller layout: reset to default",
            "[INPUT] keyboard layout: reset to default",
        ],
        "settings": [
            "[INPUT] remap.cfg: pad_b=flipper is no input known; B keeps its default",
            "[INPUT] remap.cfg: pad_y=guide is not one a layout can have; Y keeps its default",
            "[INPUT] remap.cfg: pad_start=back is not one a layout can have; Start keeps its default",
            "[INPUT] controller layout: A is X (remap.cfg)",
            "[INPUT] controller layout: Right stick click is R3 (remap.cfg)",
            "[INPUT] controller layout: Toggle free camera is None (remap.cfg)",
            "[INPUT] remap.cfg: key_lthumb=F9 is not one a layout can have; Left stick click keeps its default",
            "[INPUT] remap.cfg: key_rthumb=Hyper is no input known; Right stick click keeps its default",
            "[INPUT] keyboard layout: A is Space, Left Shift (remap.cfg)",
        ],
        "hotkey": [
            "[INPUT] controller layout: R trigger is R3", "[INPUT] controller layout: Toggle free camera is RT",
            "[INPUT] controller layout: A is RT", "[INPUT] controller layout: Toggle free camera is A",
            "[INPUT] controller layout: A is R3", "[INPUT] controller layout: R trigger is RT",
        ],
    }
    for case in ("defaults", "pad", "keys", "move", "binds", "bind", "bind_key", "used",
                 "rows", "bind_rows", "reset", "settings", "old_cfg", "hotkey"):
        run = subprocess.run([exe, case], capture_output=True, text=True, timeout=120)
        print(run.stdout + run.stderr, end="")
        if run.returncode != 0 or f"input_layout {case}: pass" not in run.stdout:
            sys.exit(f"input_layout {case}: exit {run.returncode}")
        lines = run.stderr.splitlines()
        at = 0
        for want in said.get(case, []):
            if want not in lines[at:]:
                sys.exit(f"input_layout {case}: the log doesn't say, in order: {want}")
            at = lines.index(want, at) + 1
        if case in ("rows", "old_cfg") and any(line.startswith("[INPUT]") for line in lines):
            sys.exit(f"input_layout {case}: the defaults said something in the log")
        if case == "settings" and any("key_freecam" in line for line in lines):
            sys.exit("input_layout settings: the keyboard read a free camera line")
print("input_layout: all cases pass")
