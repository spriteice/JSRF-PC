"""The in-game menu, without a window, GPU or SDL.

Cut by text: the input layer's gate from src/input/xinput_device.c,
unchanged, as that file doesn't link without the rest of the input layer;
and the window's side of the menu from src/menu/menu_sdl.cpp, unchanged,
as that file needs SDL, a window and OpenGL.

1. The menu's own drawing (src/menu/menu.cpp, with its look,
   menu_style.cpp), driven with a pad: compiled with Dear ImGui's core and
   its null backends (third_party/imgui) as the engine builds them -- no
   exceptions, no RTTI -- and test_main.cpp.
2. The window's side (xbox_MenuFrame, xbox_MenuDraw and xbox_MenuBusy, with
   set_open and the state they share, from src/menu/menu_sdl.cpp), with the
   same and test_window.cpp, which stands in for SDL and the backends.
3. The input layer's gate (menu_gate in src/input/xinput_device.c), taken
   from that file verbatim, with test_gate.c.

Requires a C and a C++ compiler on PATH: cc and c++, or CC and CXX.
"""
import os
import pathlib
import shlex
import subprocess
import sys
import tempfile

root = pathlib.Path(__file__).resolve().parents[2]
imgui = root / "third_party/imgui"
here = root / "tests/menu"
cc = shlex.split(os.environ.get("CC", "cc"))
cxx = shlex.split(os.environ.get("CXX", "c++"))

device = (root / "src/input/xinput_device.c").read_text(encoding="utf-8")
start = device.index("extern int nv_window_menu_open(void)")
gate = device[start:device.index("DWORD xbox_InputGetState", start)]
gate_source = ('#include <string.h>\n#include "input/xinput_xbox.h"\n' + gate
               + (here / "test_gate.c").read_text(encoding="utf-8"))


def cut(text, start, end):
    at = text.index(start)
    return text[at:text.index(end, at)]


window = (root / "src/menu/menu_sdl.cpp").read_text(encoding="utf-8")
window_cut = (cut(window, "#define OV_GAME_LINES", "/* ---- File:")
              + cut(window, "/* With g_mu held. */", "/* ---- the calls from the window")
              + cut(window, "static void scale_to_window(void)", 'extern "C" int xbox_MenuIsOpen(void)'))

with tempfile.TemporaryDirectory(prefix="menu-") as tmp:
    tmp = pathlib.Path(tmp)
    threads = [] if sys.platform == "win32" else ["-pthread"]
    flags = ["-std=c++11", "-O1", "-Wall", "-Wextra",
             "-fno-exceptions", "-fno-rtti", "-fno-threadsafe-statics",
             "-DIMGUI_DISABLE_DEMO_WINDOWS", "-DIMGUI_DISABLE_DEBUG_TOOLS",
             "-DIMGUI_DISABLE_DEFAULT_SHELL_FUNCTIONS",
             "-I" + str(root / "src/menu"), "-I" + str(root / "src"), "-I" + str(imgui)]
    # Built once, for both tests.
    shared = [root / "src/menu/menu.cpp", root / "src/menu/menu_style.cpp",
              imgui / "imgui.cpp", imgui / "imgui_draw.cpp",
              imgui / "imgui_tables.cpp", imgui / "imgui_widgets.cpp",
              imgui / "backends/imgui_impl_null.cpp"]
    objects = []
    for source in shared:
        objects.append(str(tmp / (source.stem + ".o")))
        subprocess.run(cxx + flags + ["-c", str(source), "-o", objects[-1]], check=True)

    menu = tmp / "menu_test"
    subprocess.run(cxx + flags + ["-o", str(menu), str(here / "test_main.cpp")] + objects + threads,
                   check=True)
    run = subprocess.run([str(menu)], timeout=60, capture_output=True, text=True)
    sys.stderr.write(run.stderr)
    if run.returncode:
        sys.exit(run.returncode)
    # The rows that say the wrong thing about File > Presets, said in the log
    # (menu.cpp's take_queued) once a check, as a row without a hint is: the
    # first that says nothing, and the first a preset can't set (the button
    # after it isn't said, but is left alone too, or the test would have
    # failed setting it). And a preset's lines, one a row it changes.
    for line in ["Says nothing: nothing said about presets",
                 "Asks, keeps nothing: asks first, and keeps nothing for a preset",
                 "[MENU] preset: Scale (Restart) to 1x, from the next start",
                 "[MENU] preset: Filtering to 4x\n"]:
        if run.stderr.count(line) != 1:
            sys.exit(f"menu: {line!r} is in the log {run.stderr.count(line)} times, not once")

    (tmp / "menu_sdl_cut.inc").write_text(window_cut, encoding="utf-8")
    window_exe = tmp / "window_test"
    subprocess.run(cxx + flags + ["-I" + str(tmp), "-o", str(window_exe), str(here / "test_window.cpp")]
                   + objects + threads, check=True)
    subprocess.run([str(window_exe)], check=True, timeout=60)

    gate_exe = pathlib.Path(tmp) / "gate_test"
    subprocess.run(cc + ["-std=c11", "-Wall", "-Wextra", "-I" + str(root / "src"),
                         "-x", "c", "-", "-o", str(gate_exe)],
                   input=gate_source, text=True, check=True)
    subprocess.run([str(gate_exe)], check=True, timeout=10)
