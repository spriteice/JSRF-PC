"""The menu's look (src/menu/menu_style.cpp), without a window, GPU or SDL.

Compiles src/menu/menu_style.cpp with Dear ImGui's core (third_party/imgui),
as the engine and the launcher build them -- no exceptions, no RTTI -- and
test_main.cpp, which checks the style it makes: no colour left in the dark
style's blue, white text readable on everything it is drawn on, the bright
marks plain on the dark, and the sizes the menu is laid out with -- a row
drawn through Dear ImGui's null backends, for its rounded corners.

Requires a C++ compiler on PATH: c++, or CXX.
"""
import os
import pathlib
import shlex
import subprocess
import sys
import tempfile

root = pathlib.Path(__file__).resolve().parents[2]
imgui = root / "third_party/imgui"
here = root / "tests/menu_style"
cxx = shlex.split(os.environ.get("CXX", "c++"))

with tempfile.TemporaryDirectory(prefix="menu_style-") as tmp:
    exe = pathlib.Path(tmp) / "menu_style_test"
    threads = [] if sys.platform == "win32" else ["-pthread"]
    subprocess.run(cxx + ["-std=c++11", "-O1", "-Wall", "-Wextra",
                          "-fno-exceptions", "-fno-rtti", "-fno-threadsafe-statics",
                          "-DIMGUI_DISABLE_DEMO_WINDOWS", "-DIMGUI_DISABLE_DEBUG_TOOLS",
                          "-DIMGUI_DISABLE_DEFAULT_SHELL_FUNCTIONS",
                          "-I" + str(root / "src/menu"), "-I" + str(imgui),
                          "-o", str(exe),
                          str(here / "test_main.cpp"), str(root / "src/menu/menu_style.cpp"),
                          str(imgui / "imgui.cpp"), str(imgui / "imgui_draw.cpp"),
                          str(imgui / "imgui_tables.cpp"), str(imgui / "imgui_widgets.cpp"),
                          str(imgui / "backends/imgui_impl_null.cpp")] + threads,
                   check=True)
    subprocess.run([str(exe)], check=True, timeout=60)
