"""The start-up shader build and the freeze watch together (known bug 45):
a long build that keeps showing its box isn't a freeze, and one that stops is.

Cut by text: from src/nv2a_gl/nv2a_gl.c, which doesn't link on its own,
sdl_show, sdl_shader_cache, nv2a_gl_progress and nv2a_gl_flips, verbatim,
against stand-ins for the window, the box, the shader cache and the backend
(gl_prelude.c, test_main.c); and from src/kernel/kernel_bridge.c, which
doesn't either, kernel_freeze_watch, verbatim, against stand-ins for the
counts its report prints and for the table of threads (kernel_prelude.c).
The one change to what is cut: both read the test's own clock, the watch
through a GetTickCount64 that kernel_prelude.c defines, so that ten seconds
of a build take no time to test. Splitting either file is b3 row 9.
The two are built as two files, as they are in the engine.
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
gl = (root / "src/nv2a_gl/nv2a_gl.c").read_text(encoding="utf-8")
bridge = (root / "src/kernel/kernel_bridge.c").read_text(encoding="utf-8")
here = root / "tests/shader_build_watch"


def cut(text, start, end):
    return text[text.index(start):text.index(end, text.index(start))]


renderer = ((here / "gl_prelude.c").read_text(encoding="utf-8") + "\n"
            + cut(gl, "/* A finished frame to the window, the way frames go",
                  "#else\n\nstatic int gl_init(")
            + cut(gl, "/*\n * With the cache open, every program in it built again", "#endif")
            + cut(gl, "/* One number that must keep changing", "void nv2a_gl_report(void)")
            + "uint32_t nv2a_gl_flips(void);\n"
            + cut(gl, "uint32_t nv2a_gl_flips(void) {", "\n") + "\n"
            + (here / "test_main.c").read_text(encoding="utf-8"))
kernel = ((here / "kernel_prelude.c").read_text(encoding="utf-8") + "\n"
          + cut(bridge, "/* Say so when the picture stops", "static void kthread_report(void)\n{")
          + "void test_watch(void);\n"
          + "void test_watch(void) { kernel_freeze_watch(); }\n")

with tempfile.TemporaryDirectory(prefix="shader_build_watch-") as directory:
    directory = pathlib.Path(directory)
    (directory / "renderer.c").write_text(renderer, encoding="utf-8")
    (directory / "kernel.c").write_text(kernel, encoding="utf-8")
    exe = str(directory / "shader_build_watch.exe")
    subprocess.run(cc + ["-std=gnu11", "-O2", "-DNDEBUG", "-Wall", "-Wextra", "-Werror",
                         "-I", "src/nv2a_gl", str(directory / "renderer.c"),
                         str(directory / "kernel.c"), "-lm", "-o", exe],
                   cwd=root, check=True)
    try:
        run = subprocess.run([exe, str(directory / "stderr.txt")], capture_output=True,
                             text=True, timeout=60)
    except subprocess.TimeoutExpired:
        sys.exit("shader_build_watch: no end in 60 s")
    text = run.stdout + run.stderr
    print(text, end="")
    if run.returncode != 0 or "shader_build_watch: all cases pass" not in text:
        sys.exit(f"shader_build_watch: exit {run.returncode}")
