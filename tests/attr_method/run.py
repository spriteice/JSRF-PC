"""Check that attr_method turns away only methods imm_method would turn away.

imm_method (src/nv2a_gl/nv2a_gl.c, from upstream 0.5.6) handles the per-vertex
attribute methods. It asks attr_method first, so that every other method, the
index words among them, leaves at once. This takes the code from
"#define MAX_IMM" up to do_draw's declaration verbatim, builds it twice, as it
is and without that first test, and has test_main.c send both every method
address, in and out of Begin/End: each must handle the same methods and leave
the same state. A per-vertex method that upstream adds outside attr_method's
ranges fails here.
Requires a C compiler on PATH (cc, or CC).
"""
import os
import pathlib
import re
import shlex
import subprocess
import tempfile

root = pathlib.Path(__file__).resolve().parents[2]
cc = shlex.split(os.environ.get("CC", "cc"))
gl = (root / "src/nv2a_gl/nv2a_gl.c").read_text(encoding="utf-8")
names = ("NUM_ATTRS", "M_SET_VERTEX3F", "M_SET_VERTEX_ARRAY_OFFSET",
         "M_SET_VERTEX_DATA2F_M", "M_SET_TEXTURE_OFFSET")
defines = "".join(re.search(r"^#define %s\b.*\n" % n, gl, re.M).group(0) for n in names)
imm = gl[gl.index("#define MAX_IMM"):gl.index("static void do_draw(void);")]
guard = "    if (!attr_method(method)) return 0;\n"
if guard not in imm:
    raise SystemExit("attr_method: imm_method no longer starts with its attr_method test")
head = """#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
""" + defines + """
static struct { float const_attr[NUM_ATTRS][4], const_set[NUM_ATTRS][4]; uint32_t prim; } S;
"""
main = (root / "tests/attr_method/test_main.c").read_text(encoding="utf-8")

with tempfile.TemporaryDirectory(prefix="attr-method-") as tmp:
    out = {}
    for name, code in (("guarded", imm), ("unguarded", imm.replace(guard, ""))):
        exe = str(pathlib.Path(tmp) / name)
        subprocess.run(cc + ["-std=c11", "-O1", "-Wall", "-Wno-unused-function",
                             "-x", "c", "-", "-o", exe],
                       input=head + code + main, text=True, check=True)
        out[name] = subprocess.run([exe], capture_output=True, text=True, check=True,
                                   timeout=60).stdout
    handled = out["guarded"].count(" 1 ")
    if out["guarded"] != out["unguarded"]:
        diff = [(a, b) for a, b in zip(out["guarded"].splitlines(), out["unguarded"].splitlines())
                if a != b]
        raise SystemExit("attr_method: imm_method differs with its first test, "
                         "as it is / without:\n" + "\n".join("%s | %s" % d for d in diff[:20]))
    if handled == 0:
        raise SystemExit("attr_method: imm_method handled no method at all")
print("attr_method: the same %d of %d method writes handled, with the same state"
      % (handled, len(out["guarded"].splitlines())))
