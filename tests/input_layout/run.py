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
the bumpers as Black and White and its Back none of the game's, and the keys
by their places, with J, keypad Enter and a keyboard's Back key second on A,
Start and Back. The pad: every one of the fourteen buttons the table read,
on or off together, with the triggers at their edges and half way and the
pad's Back handed over or not, gives what the table gave with the menu
there, and SDL's other buttons change nothing. The keys: every one of the
window's twenty places, held or not together, by its first key, its second
or both, over three pads, gives what kb_merge gave, and says a key was held
when one was. The movement keys: what xbox_InputKeyboardMove gave for every
one of the twenty. A layout's other uses: two inputs on a control, the most
of them; a trigger on Start from half way; an input out of range, or none,
pressing nothing; a key no control uses, not held as far as the game knows;
Space and J each pressing A alone; a mouse button and the last key. A row's
bind: the swap, a control's own input again, a second key kept, two
controls' input, and none. The inputs a layout uses, for the latency log.
The pages: Controller layout's sixteen rows and Keyboard layout's
twenty-four, each with Reset to default, in one add, each Input's, left
alone by the presets, with a hint, and showing the defaults' inputs.
Binding through them: the swap, kept in remap.cfg line by line and back
to no line at a default, said in the log; Guide, the pad's Back, the
window's keys and a key with no name refused, changing nothing; a mouse
button; and Keyboard moves following the left stick's rows. Reset to
default on each page alone. At start, the lines the rows kept read back as
they were, and an unknown name or an input a layout can't have keeps the
default, which the log says.
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
            "[INPUT] keyboard layout: A is Space, K",
            "[INPUT] keyboard layout: B is J",
            "[INPUT] keyboard layout: Y is Mouse right",
            "[INPUT] keyboard layout: Left stick up is I",
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
            "[INPUT] remap.cfg: key_lthumb=F9 is not one a layout can have; Left stick click keeps its default",
            "[INPUT] remap.cfg: key_rthumb=Hyper is no input known; Right stick click keeps its default",
            "[INPUT] keyboard layout: A is Space, K (remap.cfg)",
        ],
    }
    for case in ("defaults", "pad", "keys", "move", "binds", "bind", "used",
                 "rows", "bind_rows", "reset", "settings"):
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
        if case == "rows" and any(line.startswith("[INPUT]") for line in lines):
            sys.exit("input_layout rows: the defaults said something in the log")
print("input_layout: all cases pass")
