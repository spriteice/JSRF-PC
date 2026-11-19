"""tools/compare_engines.py on small builds made up here.

Each test compiles made-up C twice, as two builds of an engine, with GCC
for Linux's objects and clang for Windows' (COFF, --target
x86_64-w64-windows-gnu, which needs no headers), as CI's `linux tests`
image has both, with binutils' objdump.
"""
import shutil
import subprocess

import pytest

from tools import compare_engines

COMPILERS = {
    "linux": ["gcc", "-O2", "-c"],
    "windows": ["clang", "--target=x86_64-w64-windows-gnu", "-O2", "-c"],
}
needs_objdump = pytest.mark.skipif(shutil.which("objdump") is None, reason="no objdump")

A_BASE = """\
int printf(const char *, ...);
static int counter;
static int helper(int x) { printf("helper %d\\n", x); return x * 3 + 1; }
int other(int y) { printf("other %d\\n", y); return y + 7; }
int user(int z) { counter++; return helper(z) + other(z); }
int last(int w) { printf("last %d\\n", w); return counter + w; }
"""

# helper moved to b.c, gone extern, as are the statics both files now share.
A_MOVED = """\
int printf(const char *, ...);
int helper(int x);
int counter;
int other(int y) { printf("other %d\\n", y); return y + 7; }
int user(int z) { counter++; return helper(z) + other(z); }
int last(int w) { printf("last %d\\n", w); return counter + w; }
"""
B_MOVED = """\
int printf(const char *, ...);
int helper(int x) { printf("helper %d\\n", x); return x * 3 + 1; }
"""


def build(folder, platform, sources, archive=None, flags=()):
    """Compile each source as CMake would put it, into folder; with
    archive, the objects go in that static library instead."""
    compiler = COMPILERS[platform] + list(flags)
    if shutil.which(compiler[0]) is None:
        pytest.skip("no %s" % compiler[0])
    suffix = ".o" if platform == "linux" else ".obj"
    objects = folder / "CMakeFiles" / "engine.dir"
    objects.mkdir(parents=True)
    made = []
    for name, text in sources.items():
        src = folder / "src" / name
        src.parent.mkdir(exist_ok=True)
        src.write_text(text)
        out = objects / (name + suffix)
        subprocess.run(compiler + [str(src), "-o", str(out)], check=True)
        made.append(out)
    shutil.rmtree(folder / "src")
    if archive:
        subprocess.run(["ar", "rcD", str(folder / archive)] + [str(m) for m in made],
                       check=True)
        for m in made:
            m.unlink()
    return folder


def run(capsys, old, new, *args):
    code = compare_engines.main(["compare_engines.py", str(old), str(new), *args])
    return code, capsys.readouterr()


def kinds(out):
    """{function: kind} from the report's listed sections."""
    found, kind = {}, None
    for line in out.split("\n"):
        if line.startswith("== ") and "Symbols" not in line:
            kind = line[3:].rsplit(" (", 1)[0].lower()
        elif line.startswith("== "):
            kind = None
        elif kind and line.startswith("  ") and not line.startswith("    "):
            found[line.strip().split(",")[0]] = kind
    return found


@needs_objdump
@pytest.mark.parametrize("platform", ["linux", "windows"])
def test_the_same_engine(tmp_path, capsys, platform):
    old = build(tmp_path / "old", platform, {"a.c": A_BASE})
    new = build(tmp_path / "new", platform, {"a.c": A_BASE})
    code, out = run(capsys, old, new)
    assert code == 0, out.out
    assert "Objects: 1 the same, 0 differ" in out.out
    assert "Result: the same engine, every object byte for byte" in out.out


@needs_objdump
@pytest.mark.parametrize("platform", ["linux", "windows"])
def test_a_static_moved_and_gone_extern(tmp_path, capsys, platform):
    old = build(tmp_path / "old", platform, {"a.c": A_BASE})
    new = build(tmp_path / "new", platform, {"a.c": A_MOVED, "b.c": B_MOVED})
    code, out = run(capsys, old, new, "--extern", "helper")
    assert code == 0, out.out
    assert "Result: every difference is one a move makes" in out.out
    found = kinds(out.out)
    assert found["user"] == "a call where a body was inlined"
    assert found["helper"] == "only in one engine"
    assert "other" not in found and "last" not in found      # the same
    assert "user, CMakeFiles/engine.dir/a.c" in out.out
    assert "(calls on one side only: helper)" in out.out
    assert "counter: 4 b CMakeFiles/engine.dir/a.c" in out.out
    # Without --extern, a global function only in the new engine is listed.
    code, out = run(capsys, old, new)
    assert code == 1
    assert kinds(out.out)["helper"] == "anything else"


@needs_objdump
@pytest.mark.parametrize("platform", ["linux", "windows"])
def test_a_function_moved_as_it_was(tmp_path, capsys, platform):
    old = build(tmp_path / "old", platform, {"a.c": A_BASE})
    a_new = A_BASE.replace('int last(int w) { printf("last %d\\n", w); return counter + w; }\n',
                           "int get_counter(void) { return counter; }\n")
    b_new = ('int printf(const char *, ...);\nint get_counter(void);\n'
             'int last(int w) { printf("last %d\\n", w); return get_counter() + w; }\n')
    new = build(tmp_path / "new", platform, {"a.c": a_new, "b.c": b_new})
    code, out = run(capsys, old, new)
    found = kinds(out.out)
    # last now calls an accessor that didn't exist: a change, listed.
    assert found["last"] == "anything else"
    assert found["get_counter"] == "anything else"
    assert code == 1
    # The other functions' strings moved in their section and are the same.
    assert "other" not in found and "user" not in found


@needs_objdump
@pytest.mark.parametrize("platform", ["linux", "windows"])
def test_strings_in_a_new_order_are_no_difference(tmp_path, capsys, platform):
    old = build(tmp_path / "old", platform, {"a.c": A_BASE})
    # other moved to b.c whole: a.c's strings and functions shift.
    a_new = A_BASE.replace('int other(int y) { printf("other %d\\n", y); return y + 7; }\n',
                           "int other(int y);\n")
    b_new = 'int printf(const char *, ...);\nint other(int y) { printf("other %d\\n", y); return y + 7; }\n'
    new = build(tmp_path / "new", platform, {"a.c": a_new, "b.c": b_new})
    code, out = run(capsys, old, new)
    found = kinds(out.out)
    assert found["other"] == "moved or gone extern"
    assert "last" not in found
    assert found.get("user") in (None, "a call where a body was inlined")
    assert code == 0, out.out


@needs_objdump
@pytest.mark.parametrize("platform", ["linux", "windows"])
def test_a_constant_changed_is_listed(tmp_path, capsys, platform):
    old = build(tmp_path / "old", platform, {"a.c": A_BASE})
    new = build(tmp_path / "new", platform, {"a.c": A_BASE.replace("counter + w", "counter - w")})
    code, out = run(capsys, old, new)
    assert code == 1
    assert kinds(out.out) == {"last": "anything else"}
    assert "Result: 1 functions differ in a way a move doesn't" in out.out


@needs_objdump
def test_a_string_changed_is_listed(tmp_path, capsys):
    old = build(tmp_path / "old", "linux", {"a.c": A_BASE})
    new = build(tmp_path / "new", "linux", {"a.c": A_BASE.replace('"last %d', '"Last %d')})
    code, out = run(capsys, old, new)
    assert code == 1
    assert kinds(out.out)["last"] == "anything else"
    assert '+lea <"Last %d\\n">(%rip),%rdi' in out.out


@needs_objdump
@pytest.mark.skipif(shutil.which("ar") is None, reason="no ar")
def test_objects_in_a_static_library(tmp_path, capsys):
    old = build(tmp_path / "old", "linux", {"a.c": A_BASE}, archive="libengine.a")
    new = build(tmp_path / "new", "linux", {"a.c": A_MOVED, "b.c": B_MOVED},
                archive="libengine.a")
    code, out = run(capsys, old, new, "--extern", "helper")
    assert code == 0, out.out
    assert "  differ: libengine.a(a.c.o)" in out.out
    assert "  only in the new: libengine.a(b.c.o)" in out.out


@needs_objdump
def test_out_writes_each_function_and_diff(tmp_path, capsys):
    old = build(tmp_path / "old", "linux", {"a.c": A_BASE})
    new = build(tmp_path / "new", "linux", {"a.c": A_MOVED, "b.c": B_MOVED})
    code, out = run(capsys, old, new, "--extern", "helper", "--out", str(tmp_path / "out"))
    assert code == 0, out.out
    written = sorted(p.name for p in (tmp_path / "out").iterdir())
    assert "user.diff" in written and "user.old.txt" in written and "user.new.txt" in written
    assert "helper.new.txt" in written and "helper.old.txt" not in written
    assert "last.diff" not in written


def test_members_of_a_library():
    def member(name, body):
        return (name.ljust(16) + "0".ljust(12) + "0".ljust(6) + "0".ljust(6)
                + "644".ljust(8) + str(len(body)).ljust(10) + "`\n").encode() + body + \
            (b"\n" if len(body) % 2 else b"")
    longnames = b"a_rather_long_object_name.c.o/\n"
    data = (b"!<arch>\n" + member("/", b"\0\0\0\0") + member("//", longnames)
            + member("x.c.o/", b"one") + member("/0", b"two!") + member("x.c.o/", b"three"))
    got = compare_engines.members("lib.a", data)
    assert got == {"lib.a(x.c.o)": b"one", "lib.a(a_rather_long_object_name.c.o)": b"two!",
                   "lib.a(x.c.o#2)": b"three"}


@pytest.mark.parametrize("make, says", [
    (lambda p: None, "is not a folder"),
    (lambda p: p.mkdir(), "no objects in"),
    (lambda p: (p.mkdir(), (p / "x.o").write_bytes(b"not an object")), "isn't an x86-64"),
])
def test_what_it_cannot_compare(tmp_path, capsys, make, says):
    old = tmp_path / "old"
    old.mkdir()
    (old / "x.o").write_bytes(b"something else")
    new = tmp_path / "new"
    make(new)
    code, out = run(capsys, old, new)
    assert code == 2
    assert says in out.err


# The move puts a file between a caller and a small function it called in
# the same file, which was inlined into it there.
WRAP_BASE = """\
int report(int i);
int wrap(int i) { return report(i); }
int thread(int n) { int t = 0; for (int i = 0; i < n; i++) t += wrap(i) * 3; return t; }
"""
WRAP_MOVED = {"a.c": "int report(int i);\nint wrap(int i) { return report(i); }\n",
              "b.c": "int wrap(int i);\n"
                     "int thread(int n) { int t = 0; for (int i = 0; i < n; i++) "
                     "t += wrap(i) * 3; return t; }\n"}


@needs_objdump
@pytest.mark.parametrize("platform", ["linux", "windows"])
def test_a_caller_moved_away_from_what_it_inlined(tmp_path, capsys, platform):
    old = build(tmp_path / "old", platform, {"a.c": WRAP_BASE})
    new = build(tmp_path / "new", platform, WRAP_MOVED)
    code, out = run(capsys, old, new)
    assert code == 0, out.out
    assert kinds(out.out)["thread"] == "a call where a body was inlined"
    assert "(calls on one side only: report, wrap)" in out.out


TICK_BASE = """\
int printf(const char *, ...);
static __attribute__((noinline)) int tick(int x) { printf("tick %d\\n", x); return x + 1; }
int loop(int n) { int t = 0; for (int i = 0; i < n; i++) t += tick(i); return t; }
"""
TICK_MOVED = {"a.c": "int printf(const char *, ...);\n"
                     "__attribute__((noinline)) int tick(int x) "
                     "{ printf(\"tick %d\\n\", x); return x + 1; }\n",
              "b.c": "int tick(int x);\n"
                     "int loop(int n) { int t = 0; for (int i = 0; i < n; i++) "
                     "t += tick(i); return t; }\n"}


@needs_objdump
def test_a_static_gone_extern_starts_with_endbr64(tmp_path, capsys):
    flags = ["-fcf-protection=full"]
    old = build(tmp_path / "old", "linux", {"a.c": TICK_BASE}, flags=flags)
    new = build(tmp_path / "new", "linux", TICK_MOVED, flags=flags)
    code, out = run(capsys, old, new, "--extern", "tick", "--out", str(tmp_path / "out"))
    assert code == 0, out.out
    assert kinds(out.out)["tick"] == "moved or gone extern"
    assert "endbr64" not in (tmp_path / "out" / "tick.old.txt").read_text()
    assert "endbr64" in (tmp_path / "out" / "tick.new.txt").read_text()


PICK_BASE = """\
static __attribute__((noinline)) int one(int x) { return x * 5 + 1; }
typedef int (*fn)(int);
fn pick(void) { return one; }
"""
PICK_MOVED = {"a.c": "int one(int x);\ntypedef int (*fn)(int);\n"
                     "fn pick(void) { return one; }\n",
              "b.c": "__attribute__((noinline)) int one(int x) { return x * 5 + 1; }\n"}


@needs_objdump
def test_an_address_from_the_got_is_the_same_address(tmp_path, capsys):
    flags = ["-fPIE"]
    old = build(tmp_path / "old", "linux", {"a.c": PICK_BASE}, flags=flags)
    new = build(tmp_path / "new", "linux", PICK_MOVED, flags=flags)
    code, out = run(capsys, old, new, "--extern", "one", "--out", str(tmp_path / "out"))
    assert code == 0, out.out
    assert "pick" not in kinds(out.out)
    assert "lea <one>(%rip)" in (tmp_path / "out" / "pick.new.txt").read_text()


SWITCH = """\
int f0(void); int f1(void); int f2(void); int f3(void); int f4(void); int f5(void);
int choose(int x)
{
    switch (x) {
    case 0: return f0(); case 1: return f1() + 1; case 2: return f2() * 3;
    case 3: return f3() - 4; case 4: return f4() ^ 5; case 5: return f5() + 6;
    }
    return -1;
}
"""
FIRST = """\
int printf(const char *, ...);
int first(int x) { printf("first %d %d\\n", x, x * x); return x * 7 + 3; }
"""

# For Windows, the switch table as a newer clang (llvm-mingw's) makes it,
# in .rdata, each entry its case's place relative to the table: clang 14,
# in CI's image, puts it in the function's code instead.
FIRST_ASM = """\
__asm__(".text\\n.globl first\\nfirst:\\n    lea 7(%rcx,%rcx,2), %eax\\n    imul %ecx, %eax\\n"
        "    add $3, %eax\\n    ret\\n");
"""
SWITCH_ASM = """\
__asm__(".text\\n.globl choose\\nchoose:\\n"
        "    lea .Ltable(%rip), %rax\\n    movslq (%rax,%rcx,4), %rcx\\n"
        "    add %rax, %rcx\\n    jmp *%rcx\\n"
        ".Lcase0:\\n    mov $10, %eax\\n    ret\\n"
        ".Lcase1:\\n    mov $11, %eax\\n    ret\\n"
        ".Lcase2:\\n    mov $12, %eax\\n    ret\\n"
        ".section .rdata,\\"dr\\"\\n.p2align 2\\n.Ltable:\\n"
        "    .long .Lcase0-.Ltable\\n    .long .Lcase1-.Ltable\\n    .long .Lcase2-.Ltable\\n");
"""


@needs_objdump
@pytest.mark.parametrize("platform", ["linux", "windows"])
def test_a_switch_table_moving_with_its_code(tmp_path, capsys, platform):
    first, switch = (FIRST, SWITCH) if platform == "linux" else (FIRST_ASM, SWITCH_ASM)
    old = build(tmp_path / "old", platform, {"a.c": first + switch})
    new = build(tmp_path / "new", platform, {"a.c": switch, "b.c": first})
    code, out = run(capsys, old, new, "--out", str(tmp_path / "out"))
    assert code == 0, out.out
    assert kinds(out.out) == {"first": "moved or gone extern"}
    # Its entries are named by the section they point into, not by bytes.
    assert "[.text][.text]>>(%rip)" in (tmp_path / "out" / "choose.new.txt").read_text()


@needs_objdump
def test_a_string_past_ascii_is_read_whole(tmp_path, capsys):
    dash = chr(0x2014)                # UTF-8 that isn't ASCII
    base = A_BASE.replace('"last %d', '"last ' + dash + ' %d')
    old = build(tmp_path / "old", "linux", {"a.c": base})
    new = build(tmp_path / "new", "linux", {"a.c": base.replace(dash + " %d", dash + " %u")})
    code, out = run(capsys, old, new)
    assert code == 1, out.out
    assert '+lea <"last \\u2014 %u\\n">(%rip),%rdi' in out.out
