"""tools/gen_diff.py on two folders of generated code made up here.

Nothing of the game's is in them: the functions are written as the
recompiler writes them (translator.py: a banner with "Original: 0x...",
then the definition; the dispatch table's entries), with made-up bodies.
"""
import contextlib
import io

from tools import gen_diff

HEADER = '''/**
 * A title - Mechanically Translated Game Code
 */
#define RECOMP_GENERATED_CODE
#include "recomp_types.h"
#include <math.h>

/* Forward declarations */
{decls}
'''


def function(name, addr, body, category="game_engine"):
    return (f"/**\n * {name}\n * Original: 0x{addr:08X} - 0x{addr + 16:08X} (16 bytes, 4 insns)\n"
            f" * Category: {category}\n */\nvoid {name}(void)\n{{\n{body}}}\n\n")


def split_file(functions):
    decls = "\n".join(f"void {f.split('void ', 1)[1].split('(')[0]}(void);" for f in functions)
    return HEADER.format(decls=decls) + "".join(functions)


def dispatch(entries):
    rows = "".join(f"    {{ 0x{a:08X}u, (recomp_func_t){n} }},\n" for a, n in entries)
    return ('#define RECOMP_DISPATCH_H\n#include "recomp_funcs.h"\n'
            "typedef void (*recomp_func_t)(void);\n"
            "static const recomp_entry_t g_recomp_table[] = {\n" + rows + "};\n"
            f"static const size_t g_recomp_table_size = {len(entries)};\n")


BODY = "    uint32_t ebp;\n    ebp = g_ebp;  /* frameless */\nloc_{a:08X}: ;\n    g_eax = {v};\n    return;\n"


def body(addr, value=1, comment="frameless"):
    return BODY.format(a=addr, v=value).replace("frameless", comment)


def make(tmp, files):
    tmp.mkdir()
    for name, text in files.items():
        (tmp / name).write_text(text, encoding="utf-8")
    return tmp


def folders(tmp_path):
    same = function("sub_00011000", 0x11000, body(0x11000))
    moved = function("sub_00011100", 0x11100, body(0x11100))
    kept = make(tmp_path / "kept", {
        "recomp_0000.c": split_file([
            same,
            function("sub_00011200", 0x11200, body(0x11200, 1)),                      # code changes
            function("sub_00011300", 0x11300, body(0x11300), "game_engine"),           # banner changes
            function("sub_00011400", 0x11400, body(0x11400)),                          # spacing changes
            function("sub_00011500", 0x11500, body(0x11500)),                          # removed
            function("sub_00011600_gen", 0x11600, body(0x11600, 1)),                   # wrapped, changes
            function("SomeLibraryCall", 0x11700, body(0x11700, 1)),                   # named, banner address
            moved,
        ]),
        "recomp_0001.c": split_file([
            "void NamedInTheTable(void)\n{\n    g_eax = 7;\n}\n\n",                       # named, table address
            "void NoAddressAnywhere(void)\n{\n    g_eax = 8;\n}\n\n",                     # no address at all
        ]),
        "recomp_dispatch.c": dispatch([(0x11000, "sub_00011000"), (0x11800, "NamedInTheTable")]),
    })
    new = make(tmp_path / "new", {
        "recomp_0000.c": split_file([
            same,
            function("sub_00011200", 0x11200, body(0x11200, 2)),
            function("sub_00011300", 0x11300, body(0x11300), "crt"),
            function("sub_00011400", 0x11400, body(0x11400).replace("    g_eax", "\tg_eax")),
            function("sub_00011600_gen", 0x11600, body(0x11600, 2)),
            function("SomeLibraryCall", 0x11700, body(0x11700, 2)),
        ]),
        "recomp_0001.c": split_file([
            moved,                                                                    # moved file, same code
            "void NamedInTheTable(void)\n{\n    g_eax = 9;\n}\n\n",
            "void NoAddressAnywhere(void)\n{\n    g_eax = 10;\n}\n\n",
            function("sub_00011900", 0x11900, body(0x11900)),                          # added
        ]),
        "recomp_dispatch.c": dispatch([(0x11000, "sub_00011000"), (0x11800, "NamedInTheTable"),
                                       (0x11900, "sub_00011900")]),
    })
    return kept, new


def test_counts_and_addresses(tmp_path):
    kept, new = folders(tmp_path)
    out = gen_diff.compare(kept, new)
    assert (out["kept"], out["new"]) == (10, 10)
    # 11000, 11100 (moved to another file) and 11400 (its spacing) are the
    # same; 11300's banner changed; the one with no address is counted.
    assert out["count"] == {"the same": 3, "code changed": 5, "comments only": 1, "added": 1, "removed": 1}
    assert out["at"]["code changed"] == [0x11200, 0x11600, 0x11700, 0x11800]
    assert out["at"]["added"] == [0x11900] and out["at"]["removed"] == [0x11500]
    assert out["no address"] == 1
    assert out["other"] == {"recomp_0000.c": (2, 0), "recomp_0001.c": (0, 2), "recomp_dispatch.c": (2, 2)}


def test_the_report_names_nothing_of_the_code(tmp_path):
    kept, new = folders(tmp_path)
    text = gen_diff.report(gen_diff.compare(kept, new))
    assert "code changed: 0x00011200 0x00011600 0x00011700 0x00011800" in text
    assert "added: 0x00011900" in text and "removed: 0x00011500" in text
    assert "  with no address:     1 (in the counts, never listed)" in text
    for leak in ("SomeLibraryCall", "NamedInTheTable", "NoAddressAnywhere", "sub_", "g_eax", "game_engine"):
        assert leak not in text


def test_the_same_code_is_the_same(tmp_path):
    kept, _ = folders(tmp_path)
    out = gen_diff.compare(kept, kept)
    assert out["count"]["the same"] == out["kept"] == 10 and sum(out["count"].values()) == 10
    assert not (any(out["at"].values()) or out["other"])
    assert "outside the functions: the same" in gen_diff.report(out)


def test_main(tmp_path):
    kept, new = folders(tmp_path)
    printed = io.StringIO()
    with contextlib.redirect_stdout(printed):
        assert gen_diff.main(["gen_diff", str(kept), str(new)]) == 0
    assert printed.getvalue().startswith("generated code: 10 functions kept, 10 new")
    with contextlib.redirect_stderr(io.StringIO()):
        assert gen_diff.main(["gen_diff", str(kept), str(tmp_path / "missing")]) == 2
        (tmp_path / "empty").mkdir()
        assert gen_diff.main(["gen_diff", str(kept), str(tmp_path / "empty")]) == 2
        assert gen_diff.main(["gen_diff", str(kept)]) == 2
