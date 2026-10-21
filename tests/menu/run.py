"""The in-game menu, without a window, GPU or SDL.

1. The menu's own drawing (src/menu/menu.cpp), driven with a pad:
   compiled with Dear ImGui's core and its null backends (third_party/imgui)
   as the engine builds them -- no exceptions, no RTTI -- and test_main.cpp.
2. The input layer's gate (menu_gate in src/input/xinput_device.c), taken
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

with tempfile.TemporaryDirectory(prefix="menu-") as tmp:
    menu = pathlib.Path(tmp) / "menu_test"
    threads = [] if sys.platform == "win32" else ["-pthread"]
    subprocess.run(cxx + ["-std=c++11", "-O1", "-Wall", "-Wextra",
                          "-fno-exceptions", "-fno-rtti", "-fno-threadsafe-statics",
                          "-DIMGUI_DISABLE_DEMO_WINDOWS", "-DIMGUI_DISABLE_DEBUG_TOOLS",
                          "-DIMGUI_DISABLE_DEFAULT_SHELL_FUNCTIONS",
                          "-I" + str(root / "src/menu"), "-I" + str(imgui),
                          "-o", str(menu),
                          str(here / "test_main.cpp"), str(root / "src/menu/menu.cpp"),
                          str(imgui / "imgui.cpp"), str(imgui / "imgui_draw.cpp"),
                          str(imgui / "imgui_tables.cpp"), str(imgui / "imgui_widgets.cpp"),
                          str(imgui / "backends/imgui_impl_null.cpp")] + threads,
                   check=True)
    subprocess.run([str(menu)], check=True, timeout=60)

    gate_exe = pathlib.Path(tmp) / "gate_test"
    subprocess.run(cc + ["-std=c11", "-Wall", "-Wextra", "-I" + str(root / "src"),
                         "-x", "c", "-", "-o", str(gate_exe)],
                   input=gate_source, text=True, check=True)
    subprocess.run([str(gate_exe)], check=True, timeout=10)
