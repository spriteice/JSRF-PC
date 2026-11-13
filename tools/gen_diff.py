"""Which generated functions a recompiler change alters.

    python3 tools/gen_diff.py KEPT NEW

KEPT and NEW are two folders of generated code (port/recomp/gen), made from
the same default.xbe by two versions of the pipeline: the kept copy, and
the one a change to tools/ or port/recomp_manual.c made. Each function is
found by its definition in any file of the folder (they move between the
split files as their number changes), and compared by its code, with its
comments and spacing left out. The report is counts and addresses only:

    generated code: 31412 functions kept, 31413 new
      the same:        31406
      code changed:        2
      comments only:       3
      renamed:             1
      added:               1
      removed:             0
      with no address:     0 (in the counts, never listed)
    outside the functions: 2 declarations or tables only in the kept copy,
    2 only in the new, in recomp_dispatch.c
    code changed: 0x00012340 0x000A1B20
    renamed: 0x00194210
    added: 0x0002F000

It never prints the generated code, a function's name or anything else of
the game's, so the report can leave the machine that made the code. A
function is known by the address in its name (sub_XXXXXXXX, sub_XXXXXXXX_gen,
and the _XXXXXXXX a name from func_id gets when it clashes), else by its
banner's "Original: 0x..." line, else by its entry in the dispatch table; one
with none of them is counted, never named. A function found under another
name at the same address, as sub_XXXXXXXX becomes sub_XXXXXXXX_gen when
port/recomp_manual.c wraps it, is renamed, not removed and added: its code
is compared without its name, and if that differs too it counts as code
changed. Standard library only. The exit status is 0 whether or not anything
changed, and 2 for a folder that can't be read.
"""
import collections
import hashlib
import pathlib
import re
import sys

SOURCES = ("*.c", "*.h")
# C at the top level, a piece at a time: comments, preprocessor lines,
# string and character literals whole, braces, semicolons, and the rest a
# line at a time, so that a # at a line's start is seen as a directive.
TOKEN = re.compile(r"""
     (?P<comment>/\*.*?\*/|//[^\n]*)
    |(?P<directive>^[ \t]*\#(?:[^\n]*\\\n)*[^\n]*)
    |(?P<literal>"(?:[^"\\\n]|\\.)*"|'(?:[^'\\\n]|\\.)*')
    |(?P<open>\{)|(?P<close>\})|(?P<semi>;)
    |(?P<code>[^/"'{};\#\n]+|\n|[/"'\#])
""", re.S | re.M | re.X)
ADDRESS_IN_NAME = re.compile(r"_([0-9A-Fa-f]{8})(?:_gen)?$")
BANNER_ADDRESS = re.compile(r"Original: 0x([0-9A-Fa-f]{8})")
DISPATCH_ENTRY = re.compile(r"\{\s*0x([0-9A-Fa-f]{8})u?\s*,\s*\(recomp_func_t\)\s*(\w+)\s*\}")
FUNCTION_HEAD = re.compile(r"(\w+)\s*\([^;{}=]*\)$")
ADDRESSES_A_LINE = 8
STATES = ("the same", "code changed", "comments only", "renamed", "added", "removed")
LISTED = ("code changed", "renamed", "added", "removed")


def _flat(pieces):
    """Pieces of C as one line, spacing made single."""
    return " ".join("".join(pieces).split())


def top_level(text):
    """The file's top-level items, in order: ("directive", text) for a
    preprocessor line, ("function", name, code, comments) for a definition
    (comments: its own and the banner before it), and ("other", code) for
    anything else (a declaration, a table)."""
    items, code, comments, depth, head = [], [], [], 0, None
    for m in TOKEN.finditer(text):
        kind, piece = m.lastgroup, m.group()
        if kind == "comment":
            comments.append(piece)
            continue
        if kind == "directive" and depth == 0:
            items.append(("directive", " ".join(piece.split())))
            continue
        code.append(piece)
        if kind == "open":
            if depth == 0:
                head = FUNCTION_HEAD.search(_flat(code[:-1]))
            depth += 1
        elif kind == "close" and depth:
            depth -= 1
            if depth == 0 and head:
                items.append(("function", head.group(1), _flat(code), _flat(comments)))
                code, comments, head = [], [], None
        elif kind == "semi" and depth == 0:
            if _flat(code) != ";":
                items.append(("other", _flat(code)))
            code, comments, head = [], [], None
    if _flat(code):
        items.append(("other", _flat(code)))
    return items


def _sha(s):
    return hashlib.sha256(s.encode("utf-8")).hexdigest()


def read_folder(folder):
    """{key: (address or None, code hash, comments hash, hash of the code
    with its own name taken out)} for each function, and {file: Counter of
    its other top-level items} for the rest."""
    folder = pathlib.Path(folder)
    files = sorted(p for pattern in SOURCES for p in folder.glob(pattern))
    if not files:
        raise SystemExit(f"gen_diff: no generated code in {folder}")
    found, other, dispatch = {}, {}, {}
    for path in files:
        text = path.read_text(encoding="utf-8", errors="replace")
        for addr, name in DISPATCH_ENTRY.findall(text):
            dispatch.setdefault(name, int(addr, 16))
        lines = collections.Counter()
        for item in top_level(text):
            if item[0] != "function":
                lines[item[1]] += 1
                continue
            _, name, code, comments = item
            key = name if name not in found else (path.name, name)
            bare = re.sub(r"\b%s\b" % re.escape(name), "", code)
            found[key] = (name, comments, _sha(code), _sha(comments), _sha(bare))
        other[path.name] = lines
    functions = {}
    for key, (name, comments, code_hash, comments_hash, bare_hash) in found.items():
        m = ADDRESS_IN_NAME.search(name)
        banner = BANNER_ADDRESS.search(comments)
        addr = (int(m.group(1), 16) if m else int(banner.group(1), 16) if banner
                else dispatch.get(name))
        functions[key] = (addr, code_hash, comments_hash, bare_hash)
    return functions, other


def compare(kept, new):
    """The comparison of two folders: how many functions are the same, have
    changed, and so on, the addresses of those changed, renamed, added and
    removed, and the other top-level items only in one, by file."""
    old_fns, old_other = read_folder(kept)
    new_fns, new_other = read_folder(new)
    out = {"kept": len(old_fns), "new": len(new_fns), "no address": 0, "other": {},
           "count": dict.fromkeys(STATES, 0), "at": {k: [] for k in LISTED}}
    # A name only in the kept copy and another only in the new, with one
    # address between them, is one function renamed.
    only = {side: collections.defaultdict(list) for side in ("kept", "new")}
    for key, fn in old_fns.items():
        if key not in new_fns and fn[0] is not None:
            only["kept"][fn[0]].append(key)
    for key, fn in new_fns.items():
        if key not in old_fns and fn[0] is not None:
            only["new"][fn[0]].append(key)
    renamed = {}
    for addr, keys in only["kept"].items():
        if len(keys) == 1 and len(only["new"].get(addr, ())) == 1:
            renamed[keys[0]] = only["new"][addr][0]
    pairs = [(key, key) for key in old_fns.keys() | new_fns.keys()
             if key not in renamed and key not in renamed.values()]
    pairs += renamed.items()
    for old_key, new_key in pairs:
        old, cur = old_fns.get(old_key), new_fns.get(new_key)
        addr = (cur or old)[0]
        if not old:
            state = "added"
        elif not cur:
            state = "removed"
        elif old_key != new_key:
            state = "renamed" if old[3] == cur[3] else "code changed"
        elif old[1:3] == cur[1:3]:
            state = "the same"
        elif old[1] == cur[1]:
            state = "comments only"
        else:
            state = "code changed"
        out["count"][state] += 1
        if addr is None:
            out["no address"] += 1
        elif state in LISTED:
            out["at"][state].append(addr)
    for name in sorted(old_other.keys() | new_other.keys()):
        a, b = old_other.get(name, collections.Counter()), new_other.get(name, collections.Counter())
        gone, came = sum((a - b).values()), sum((b - a).values())
        if gone or came:
            out["other"][name] = (gone, came)
    for addrs in out["at"].values():
        addrs.sort()
    return out


def report(out):
    lines = [f"generated code: {out['kept']} functions kept, {out['new']} new"]
    lines += [f"  {state + ':':16}{out['count'][state]:6}" for state in STATES]
    lines.append(f"  {'with no address:':16}{out['no address']:6} (in the counts, never listed)")
    if out["other"]:
        gone = sum(g for g, _ in out["other"].values())
        came = sum(c for _, c in out["other"].values())
        lines.append(f"outside the functions: {gone} declarations or tables only in the kept copy, "
                     f"{came} only in the new, in " + ", ".join(out["other"]))
    else:
        lines.append("outside the functions: the same")
    for state in LISTED:
        addrs = [f"0x{a:08X}" for a in out["at"][state]]
        for i in range(0, len(addrs), ADDRESSES_A_LINE):
            lines.append(f"{state}: " + " ".join(addrs[i:i + ADDRESSES_A_LINE]))
    return "\n".join(lines)


def main(argv):
    if len(argv) != 3:
        print(__doc__.strip().split("\n\n")[1], file=sys.stderr)
        return 2
    for folder in argv[1:]:
        if not pathlib.Path(folder).is_dir():
            print(f"gen_diff: {folder} is not a folder", file=sys.stderr)
            return 2
    try:
        print(report(compare(argv[1], argv[2])))
    except SystemExit as e:
        print(e, file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
