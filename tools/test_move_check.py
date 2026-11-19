"""tools/move_check.py on small repositories made up here.

Each test writes two trees of made-up C into a new git repository, as
trees only (no commits), and checks a move between them.
"""
import subprocess

import pytest

from tools import move_check

A_BASE = """\
/* a.c - the first file */
#include "a.h"

static int s_count;

int first(int x)
{
    return x + 1;
}

static int helper(int x)
{
    s_count++;
    return x * 3;
}

int last(int x)
{
    return helper(x) + s_count;
}
"""

# a.c with helper taken out, the rest as it was.
A_NEW = """\
/* a.c - the first file */
#include "a.h"

static int s_count;

int first(int x)
{
    return x + 1;
}

int last(int x)
{
    return helper(x) + s_count;
}
"""

B_HEAD = """\
/**
 * b.c - the helper, in a file of its own
 *
 * Moved out of a.c as it was.
 */

#include "a.h"
#include <stdio.h>

"""

HELPER = """\
static int helper(int x)
{
    s_count++;
    return x * 3;
}
"""


def git(repo, *args, data=None):
    return subprocess.run(["git", *args], cwd=repo, input=data, check=True,
                          stdout=subprocess.PIPE, text=True).stdout.strip()


@pytest.fixture
def repo(tmp_path, monkeypatch):
    git(tmp_path, "init", "-q")
    monkeypatch.chdir(tmp_path)
    return tmp_path


def tree(repo, files):
    """The files as the index, and its tree's hash."""
    git(repo, "read-tree", "--empty")
    for path, text in files.items():
        blob = git(repo, "hash-object", "-w", "--stdin", data=text)
        git(repo, "update-index", "--add", "--cacheinfo", "100644,%s,%s" % (blob, path))
    return git(repo, "write-tree")


def run(capsys, *args):
    code = move_check.main(["move_check.py", *args])
    return code, capsys.readouterr()


def test_a_clean_move_is_all_accounted_for(repo, capsys):
    base = tree(repo, {"a.c": A_BASE})
    new = tree(repo, {"a.c": A_NEW, "b.c": B_HEAD + HELPER})
    code, out = run(capsys, base, new, "--from", "a.c", "--to", "b.c")
    assert code == 0, out.out
    assert "Lines taken out of a.c: 6\n" in out.out
    assert "  in b.c word for word and in order: 5\n" in out.out
    assert "  not in a unit as they were: 1 (blank 1)\n" in out.out
    assert "Lines in b.c not moved from a.c: 9 of 14 (head comment 6, include 2, blank 1)" in out.out
    assert "Result: every line accounted for" in out.out
    assert "!" not in out.out


def test_static_taken_off_is_counted_and_named(repo, capsys):
    base = tree(repo, {"a.c": A_BASE})
    new_a = A_NEW.replace("static int s_count;", "int s_count;\nint helper(int x);")
    new = tree(repo, {"a.c": new_a,
                      "b.c": B_HEAD + "extern int s_count;\n\n" + HELPER.replace("static ", "")})
    code, out = run(capsys, base, new, "--from", "a.c", "--to", "b.c")
    assert code == 0, out.out
    assert "the same but for `static` taken off: 1\n" in out.out
    assert "  b.c:%d  int helper(int x)" % (B_HEAD.count("\n") + 3) in out.out
    assert "  a.c:4  int s_count;" in out.out
    assert "Lines added to a.c: 2 (declaration 1, static taken off 1)" in out.out


def test_a_static_put_on_is_not_a_move(repo, capsys):
    base = tree(repo, {"a.c": A_BASE.replace("static int helper", "int helper")})
    new = tree(repo, {"a.c": A_NEW, "b.c": B_HEAD + HELPER})
    code, out = run(capsys, base, new, "--from", "a.c", "--to", "b.c")
    assert code == 1
    assert "- %5d ! other    int helper(int x)" % 11 in out.out
    assert "+ %5d ! other    static int helper(int x)" % (B_HEAD.count("\n") + 1) in out.out


def test_a_rename_is_applied_and_counted(repo, capsys):
    base = tree(repo, {"a.c": A_BASE})
    new_a = A_NEW.replace("s_count", "a_count")
    new = tree(repo, {"a.c": new_a, "b.c": B_HEAD + HELPER.replace("s_count", "a_count")})
    code, out = run(capsys, base, new, "--from", "a.c", "--to", "b.c")
    assert code == 1
    code, out = run(capsys, base, new, "--from", "a.c", "--to", "b.c",
                    "--rename", "s_count=a_count")
    assert code == 0, out.out
    assert "Renamed: s_count to a_count, in 3 lines" in out.out
    assert "renamed in place 2" in out.out


def test_a_line_changed_on_the_way_is_listed(repo, capsys):
    base = tree(repo, {"a.c": A_BASE})
    new = tree(repo, {"a.c": A_NEW, "b.c": B_HEAD + HELPER.replace("x * 3", "x * 4")})
    code, out = run(capsys, base, new, "--from", "a.c", "--to", "b.c")
    assert code == 1
    assert "Result: 2 lines to explain" in out.out
    assert "- %5d ! other        return x * 3;" % 14 in out.out
    assert "! other        return x * 4;" in out.out


def test_lines_put_in_out_of_order_are_listed(repo, capsys):
    base = tree(repo, {"a.c": A_BASE})
    swapped = HELPER.replace("    s_count++;\n    return x * 3;\n",
                             "    return x * 3;\n    s_count++;\n")
    new = tree(repo, {"a.c": A_NEW, "b.c": B_HEAD + swapped})
    code, out = run(capsys, base, new, "--from", "a.c", "--to", "b.c")
    assert code == 1
    assert "in b.c word for word and in order: 4\n" in out.out
    assert "-    14 ! other        return x * 3;" in out.out
    assert "Result: 2 lines to explain" in out.out


def test_a_move_into_two_units(repo, capsys):
    base = tree(repo, {"a.c": A_BASE})
    first = "int first(int x)\n{\n    return x + 1;\n}\n"
    last = "int last(int x)\n{\n    return helper(x) + s_count;\n}\n"
    new_a = A_BASE.replace(first + "\n", "").replace("\n" + last, "")
    new = tree(repo, {"a.c": new_a, "c.c": "#include \"a.h\"\n\n" + last,
                      "b.c": "#include \"a.h\"\n\n" + first})
    code, out = run(capsys, base, new, "--from", "a.c", "--to", "c.c", "--to", "b.c")
    assert code == 0, out.out
    assert "  in c.c word for word and in order: 5\n" in out.out
    assert "  in b.c word for word and in order: 4\n" in out.out
    assert "not in a unit as they were: 1 (blank 1)" in out.out


def test_moves_from_two_files_in_one_run(repo, capsys):
    dfirst = "int dfirst(int x)\n{\n    return x - 1;\n}\n"
    dmid = "int dmid(int x)\n{\n    return x * 2;\n}\n"
    dlast = "int dlast(int x)\n{\n    return x / 2;\n}\n"
    head = "/* d.c */\n#include \"a.h\"\n\n"
    base = tree(repo, {"a.c": A_BASE, "d.c": head + dfirst + "\n" + dmid + "\n" + dlast})
    new = tree(repo, {"a.c": A_NEW, "d.c": head + dmid,
                      "b.c": B_HEAD + HELPER + "\n" + dfirst,
                      "e.c": "#include \"a.h\"\n\n" + dlast})
    code, out = run(capsys, base, new, "--from", "a.c", "d.c", "--to", "b.c", "e.c")
    assert code == 0, out.out
    # Each file's lines go where they arrived, blank lines with them.
    assert "Lines taken out of a.c: 6\n  in b.c word for word and in order: 6\n" in out.out
    assert ("Lines taken out of d.c: 10\n  in b.c word for word and in order: 4\n"
            "  in e.c word for word and in order: 5\n") in out.out
    assert "Lines in b.c not moved from a.c or d.c: 9 of 19 (head comment 6" in out.out
    assert "Lines in e.c not moved from d.c: 1 of 6 (include 1)" in out.out
    assert "Result: every line accounted for" in out.out


def test_a_move_into_a_file_already_there(repo, capsys):
    b_base = "/* b.c */\n#include \"a.h\"\n\nint other(void)\n{\n    return 2;\n}\n"
    base = tree(repo, {"a.c": A_BASE, "b.c": b_base})
    new = tree(repo, {"a.c": A_NEW, "b.c": b_base + "\n" + HELPER})
    code, out = run(capsys, base, new, "--from", "a.c", "--to", "b.c")
    assert code == 0, out.out
    assert "Lines in b.c not moved from a.c: 1 of 13 (blank 1)" in out.out


def test_an_accessor_is_listed_to_explain(repo, capsys):
    base = tree(repo, {"a.c": A_BASE})
    new_a = A_NEW.replace("    return helper(x) + s_count;",
                          "    return helper(x) + helper_count();")
    accessor = "\nint helper_count(void)\n{\n    return s_count;\n}\n"
    new = tree(repo, {"a.c": new_a, "b.c": B_HEAD + HELPER + accessor})
    code, out = run(capsys, base, new, "--from", "a.c", "--to", "b.c")
    assert code == 1
    assert "Lines added to a.c: 1 (other 1)" in out.out
    assert "! other        return helper(x) + helper_count();" in out.out
    assert "! other    int helper_count(void)" in out.out


def test_other_files_are_listed_but_not_in_the_result(repo, capsys):
    base = tree(repo, {"a.c": A_BASE, "a.h": "/* a.h */\nint first(int x);\n",
                       "CMakeLists.txt": "add(a.c)\n"})
    new = tree(repo, {"a.c": A_NEW, "b.c": B_HEAD + HELPER,
                      "a.h": "/* a.h */\nint first(int x);\n\n/* b.c */\n"
                             "int helper(int x);   /* the helper */\n"
                             "int helper_many(int x,\n                int y);\n",
                      "CMakeLists.txt": "add(a.c)\nadd(b.c)\n"})
    code, out = run(capsys, base, new, "--from", "a.c", "--to", "b.c")
    assert code == 0, out.out
    assert "  CMakeLists.txt (+1 -0: other 1)" in out.out
    assert "  a.h (+5 -0: blank 1, declaration 3, comment 1)" in out.out


def test_what_is_staged_is_checked_without_new(repo, capsys):
    base = tree(repo, {"a.c": A_BASE})
    new = tree(repo, {"a.c": A_NEW, "b.c": B_HEAD + HELPER})
    code, out = run(capsys, base, "--from", "a.c", "--to", "b.c")
    assert code == 0, out.out
    assert out.out.startswith("Move check: tree %s (staged) against %s\n" % (new[:8], base))


@pytest.mark.parametrize("args, says", [
    (["nothere", "--from", "a.c", "--to", "b.c"], "nothere is not a tree git has"),
    (["BASE", "--from", "b.c", "--to", "a.c"], "no lines were taken out of b.c"),
    (["BASE", "--from", "a.c", "--to", "c.c"], "c.c isn't in the tree"),
    (["BASE", "--from", "a.c", "--to", "a.c"], "named only once"),
    (["BASE", "--from", "a.c", "--to", "b.c", "--rename", "x"], "OLD=NEW"),
    (["BASE", "--to", "b.c"], "--from"),
])
def test_what_it_cannot_check(repo, capsys, args, says):
    base = tree(repo, {"a.c": A_BASE})
    tree(repo, {"a.c": A_NEW, "b.c": B_HEAD + HELPER})
    code, out = run(capsys, *[base if a == "BASE" else a for a in args])
    assert code == 2
    assert says in out.err


def test_comments_and_strings_are_told_apart():
    code, comments = move_check.scan([
        'const char *s = "/* not a comment */";  /* a comment */',
        "/* a comment",
        "   on two lines */",
        "int x; // after the code",
        "// only a comment",
        "char c = '\"'; /* a quote in quotes */",
    ])
    assert comments == {2, 3, 5}
    assert code[1].strip() == 'const char *s = "/* not a comment */";'
    assert code[4].strip() == "int x;"
    assert code[6].strip() == "char c = '\"';"


def test_declarations_are_found_over_lines_and_past_comments():
    lines = [
        "int one(void);   /* with a comment */",
        "extern int g_two;",
        "void three(int a,",
        "           int b);",
        "int four = 4;",
        "static int five(void)",
        "{",
        "MACRO(six);",
        "    call(seven);",
    ]
    numbered = list(enumerate(lines, 1))
    code, _ = move_check.scan(lines)
    assert move_check.declarations(numbered, code) == {1, 2, 3, 4}
