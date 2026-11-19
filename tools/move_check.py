"""Whether code moved to another file arrived as it left.

    python3 tools/move_check.py BASE [NEW] --from FILE [FILE ...]
                                --to UNIT [UNIT ...] [--rename OLD=NEW ...]

BASE and NEW are two trees git knows (a commit, or a tree hash as a review
pack names it); without NEW, what is staged is checked, and its tree hash
printed. The lines taken out of each FILE between them must be the lines
put into the units, word for word and in the same order, so that a change
moving code out of several files is checked in one run. A unit is a new
file or one that was already there; with several, each is matched in the
order given against the lines not yet matched, from the files it shares
the most lines with first, and each moved line counts once.

Allowed without a word, and counted: a moved line that differs only by a
`static` taken off its start (a static the files now share, gone extern);
a name given its unit's prefix on the way, with --rename OLD=NEW, applied
as a whole word to the lines taken out before they are matched, and to
lines that change only by it in place; and, in each FILE and the units, a
new unit's head comment, #include lines, blank lines and declarations
(prototypes and extern lines). Everything else in each FILE and the units
is listed for the review pack to explain: lines taken out that arrived
nowhere as they were, lines in a unit that weren't moved, lines added to
a FILE, and comments changed. The other files the change touches are
listed by file, with their lines sorted the same way, but don't decide
the result.

The exit status is 0 when every line in each FILE and the units is
accounted for, 1 when some are listed to explain, and 2 when it can't
run. Standard library only, and git.
"""
import argparse
import collections
import difflib
import re
import subprocess
import sys

STATIC = re.compile(r"^static\s+")
INCLUDE = re.compile(r"^\s*#\s*include\b")
HUNK = re.compile(r"^@@ -(\d+)(?:,\d+)? \+(\d+)(?:,\d+)? @@")
NAME = re.compile(r"^[A-Za-z_]\w*$")
PROTOTYPE = re.compile(r"^[A-Za-z_][\w\s\*]*?\b[A-Za-z_]\w*\s*\(.*\)\s*;$")
# The kinds a line not moved as it was can be. The first five are
# accounted for; the last two are listed to explain.
ACCOUNTED = ("head comment", "include", "blank", "declaration", "static taken off",
             "renamed in place")
TO_EXPLAIN = ("comment", "other")
SHORT = {"head comment": "head", "include": "include", "blank": "blank",
         "declaration": "decl", "static taken off": "static", "renamed in place": "renamed",
         "comment": "comment",
         "other": "other"}


class CheckError(Exception):
    pass


def git(args, cwd=None):
    p = subprocess.run(["git"] + args, cwd=cwd, stdout=subprocess.PIPE,
                       stderr=subprocess.PIPE)
    if p.returncode != 0:
        raise CheckError("git %s: %s" % (" ".join(args),
                                         p.stderr.decode("utf-8", "replace").strip()))
    return p.stdout.decode("utf-8", "surrogateescape")


def tree_of(rev, cwd):
    try:
        return git(["rev-parse", "--verify", "--quiet", rev + "^{tree}"], cwd).strip()
    except CheckError:
        raise CheckError("%s is not a tree git has" % rev)


def file_lines(tree, path, cwd):
    """The file's lines in the tree, or None where it isn't there."""
    listed = git(["ls-tree", "--name-only", tree, "--", path], cwd)
    if not listed.strip():
        return None
    return git(["cat-file", "blob", "%s:%s" % (tree, path)], cwd).split("\n")


def changed_lines(base, new, path, cwd):
    """[(line in BASE, text)] taken out of the file and [(line in NEW,
    text)] put into it, by git's own diff, in order."""
    out = git(["diff", "--no-color", "--no-ext-diff", "--no-textconv", "--no-renames",
               "--diff-algorithm=myers", "-U0", base, new, "--", path], cwd)
    gone, came, old_no, new_no = [], [], 0, 0
    for line in out.split("\n"):
        m = HUNK.match(line)
        if m:
            old_no, new_no = int(m.group(1)), int(m.group(2))
            continue
        if line.startswith("-") and not line.startswith("---"):
            gone.append((old_no, line[1:]))
            old_no += 1
        elif line.startswith("+") and not line.startswith("+++"):
            came.append((new_no, line[1:]))
            new_no += 1
    return gone, came


def scan(lines):
    """For each 1-based line, its code with the comments taken out, and
    the numbers of the lines that hold a comment and no code."""
    code_of, comments, in_block = {}, set(), False
    for no, line in enumerate(lines or (), 1):
        code, comment = [], in_block and not line.strip()
        i, quote = 0, None
        while i < len(line):
            c, pair = line[i], line[i:i + 2]
            if in_block:
                comment = True
                if pair == "*/":
                    in_block, i = False, i + 2
                    code.append(" ")
                    continue
            elif quote:
                code.append(c)
                if c == "\\" and i + 1 < len(line):
                    code.append(line[i + 1])
                    i += 1
                elif c == quote:
                    quote = None
            elif pair == "/*":
                in_block, comment, i = True, True, i + 2
                continue
            elif pair == "//":
                comment = True
                break
            else:
                if c in "\"'":
                    quote = c
                code.append(c)
            i += 1
        code_of[no] = "".join(code)
        if comment and not code_of[no].strip():
            comments.add(no)
    return code_of, comments


def comment_only(lines):
    """The 1-based numbers of the lines that hold a comment and no code."""
    return scan(lines)[1]


def head_comment(lines):
    """The 1-based numbers of a file's opening comment, with the blank
    lines in and after it."""
    comments, head = comment_only(lines), set()
    for no, line in enumerate(lines or (), 1):
        if no in comments or not line.strip():
            head.add(no)
        else:
            break
    return head


def declarations(numbered, code_of):
    """The line numbers, among [(no, text)], of the prototypes and extern
    lines: statements at the top level, over one line or several in a
    row, with no body and no value, their comments left out."""
    found, i = set(), 0
    code = [code_of.get(no, text).rstrip() for no, text in numbered]
    while i < len(numbered):
        first = code[i]
        if not (first[:1].isalpha() or first[:1] == "_"):
            i += 1
            continue
        j = i
        while (j + 1 < len(numbered) and not code[j].endswith(";")
               and numbered[j + 1][0] == numbered[j][0] + 1 and code[j + 1].strip()
               and j - i < 8):
            j += 1
        joined = " ".join(c.strip() for c in code[i:j + 1])
        if (joined.endswith(";") and not any(c in joined for c in "{}=")
                and (joined.startswith("extern ") or PROTOTYPE.match(joined))):
            found.update(n for n, _ in numbered[i:j + 1])
            i = j + 1
        else:
            i += 1
    return found


def classify(numbered, lines, head=frozenset()):
    """{no: kind} for a file's lines [(no, text)] not moved as they were;
    lines is the whole file they are in."""
    code_of, comments = scan(lines)
    decls = declarations(numbered, code_of)
    kinds = {}
    for no, text in numbered:
        if no in head:
            kinds[no] = "head comment"
        elif not text.strip():
            kinds[no] = "blank"
        elif INCLUDE.match(text):
            kinds[no] = "include"
        elif no in decls:
            kinds[no] = "declaration"
        elif no in comments:
            kinds[no] = "comment"
        else:
            kinds[no] = "other"
    return kinds


def renamer(renames):
    pats = [(re.compile(r"\b%s\b" % re.escape(old)), new) for old, new in renames]

    def apply(text):
        for pat, new in pats:
            text = pat.sub(new, text)
        return text
    return apply


def match(gone, came, key_gone, key_came):
    """Pairs (i, j) of gone[i] and came[j] whose keys are equal, in order:
    difflib's matching blocks, which for a move are its runs of lines."""
    sm = difflib.SequenceMatcher(None, [key_gone(t) for _, t in gone],
                                 [key_came(t) for _, t in came], autojunk=False)
    return [(a + k, b + k) for a, b, size in sm.get_matching_blocks() for k in range(size)]


def check(base, new, sources, units, renames=(), cwd=None):
    """The move check, as a dict the report prints."""
    base_tree = tree_of(base, cwd)
    if new is None:
        new_tree, new_name = git(["write-tree"], cwd).strip(), "staged"
    else:
        new_tree, new_name = tree_of(new, cwd), new
    named = list(sources) + list(units)
    if len(set(named)) != len(named):
        raise CheckError("a file can be named only once, as one --from or one --to")
    rename = renamer(renames)

    def key(text):
        return STATIC.sub("", text)

    out = {"base": base, "new": new_name, "new_tree": new_tree, "sources": [], "units": [],
           "static": [], "rename_counts": {o: 0 for o, _ in renames}, "renames": list(renames)}
    gone, added = {}, {}
    for source in sources:
        gone[source], added[source] = changed_lines(base_tree, new_tree, source, cwd)
        if not gone[source]:
            raise CheckError("no lines were taken out of %s between the trees" % source)
    claimed = {}           # (source, index in its lines taken out) -> (unit, line in the unit)
    moved = collections.Counter()           # (source, unit) -> lines
    static_moved = collections.Counter()    # source -> lines
    for unit in units:
        unit_base = file_lines(base_tree, unit, cwd)
        unit_new = file_lines(new_tree, unit, cwd)
        if unit_new is None:
            raise CheckError("%s isn't in the tree %s" % (unit, new_name))
        unit_gone, came = changed_lines(base_tree, new_tree, unit, cwd)
        if not came:
            raise CheckError("no lines were put into %s between the trees" % unit)
        open_lines = list(range(len(came)))
        # The sources it shares the most lines with first.
        words = {key(t) for _, t in came if t.strip()}
        order = sorted(sources, key=lambda s: -len(words & {key(rename(t)) for _, t in gone[s]}))
        for source in order:
            free = [i for i in range(len(gone[source])) if (source, i) not in claimed]
            if not free or not open_lines:
                continue
            for a, b in match([gone[source][i] for i in free], [came[j] for j in open_lines],
                              lambda t: key(rename(t)), key):
                i, j = free[a], open_lines[b]
                was, now = rename(gone[source][i][1]), came[j][1]
                if was != now and STATIC.sub("", was, count=1) != now:
                    continue          # the same only with a static put on
                claimed[(source, i)] = (unit, came[j][0])
                moved[(source, unit)] += 1
                if was != now:
                    out["static"].append((unit, came[j][0], now))
                    static_moved[source] += 1
                _count_renames(gone[source][i][1], renames, out["rename_counts"])
            taken = {no for u, no in claimed.values() if u == unit}
            open_lines = [j for j in open_lines if came[j][0] not in taken]
        rest = [came[j] for j in open_lines]
        part = _rest_of_file(unit, unit_base, unit_new, unit_gone, rest, rename,
                             out["rename_counts"], renames)
        out["static"] += part["static"]
        part["size"] = len(unit_new) - (unit_new[-1:] == [""])
        out["units"].append(part)
    for source in sources:
        left = [(no, t) for i, (no, t) in enumerate(gone[source])
                if (source, i) not in claimed]
        part = _rest_of_file(source, file_lines(base_tree, source, cwd),
                             file_lines(new_tree, source, cwd), left, added[source], rename,
                             out["rename_counts"], renames)
        out["static"] += part["static"]
        part["taken"] = len(gone[source])
        part["moved"] = [(u, moved[(source, u)]) for u in units if moved[(source, u)]]
        part["static_moved"] = static_moved[source]
        out["sources"].append(part)
    for part in out["units"]:
        part["from"] = [s for s in sources if moved[(s, part["path"])]]
    # Every other file the change touches, listed only.
    names = git(["diff", "--name-only", "--no-renames", base_tree, new_tree], cwd).split("\n")
    others = []
    for path in names:
        if not path or path in named:
            continue
        o_gone, o_came = changed_lines(base_tree, new_tree, path, cwd)
        others.append(_rest_of_file(path, file_lines(base_tree, path, cwd),
                                    file_lines(new_tree, path, cwd), o_gone, o_came,
                                    rename, None, renames))
    out["others"] = others
    return out


def _count_renames(text, renames, counts):
    if counts is None:
        return
    for old, _new in renames:
        if re.search(r"\b%s\b" % re.escape(old), text):
            counts[old] += 1


def _rest_of_file(path, base_lines, new_lines, gone, came, rename, counts, renames):
    """A file's lines taken out and put in that weren't moved: those
    changed only by a rename, and the rest sorted by kind. A file new in
    the change may have a head comment."""
    in_place = {}          # line taken out -> (line put in, kind)
    for a, b in match(gone, came, lambda t: STATIC.sub("", rename(t)),
                      lambda t: STATIC.sub("", t)):
        was, now = rename(gone[a][1]), came[b][1]
        if gone[a][1] == now:
            continue           # the same line, elsewhere in the file
        if was == now:
            in_place[a] = (b, "renamed in place")
        elif STATIC.sub("", was, count=1) == now:
            in_place[a] = (b, "static taken off")
        else:
            continue
        _count_renames(gone[a][1], renames, counts)
    gone_done = {gone[a][0]: kind for a, (b, kind) in in_place.items()}
    came_done = {came[b][0]: kind for a, (b, kind) in in_place.items()}
    head = head_comment(new_lines) if base_lines is None else frozenset()
    came_kinds = classify([(n, t) for n, t in came if n not in came_done], new_lines, head)
    came_kinds.update(came_done)
    gone_kinds = classify([(n, t) for n, t in gone if n not in gone_done], base_lines)
    gone_kinds.update(gone_done)
    static = [(path, came[b][0], came[b][1]) for a, (b, kind) in sorted(in_place.items())
              if kind == "static taken off"]
    return {"path": path, "came": came, "came_kinds": came_kinds,
            "gone": gone, "gone_kinds": gone_kinds, "static": static}


def _counts(kinds):
    order = ACCOUNTED + TO_EXPLAIN
    have = {k: 0 for k in order}
    for k in kinds.values():
        have[k] += 1
    return ", ".join("%s %d" % (k, have[k]) for k in order if have[k])


def _explain(kinds):
    return sum(1 for k in kinds.values() if k in TO_EXPLAIN)


def _listing(sign, numbered, kinds):
    rows = []
    for no, text in numbered:
        kind = kinds[no]
        mark = "!" if kind in TO_EXPLAIN else " "
        rows.append("  %s %5d %s %-8s %s" % (sign, no, mark, SHORT[kind], text))
    return rows


def report(out):
    new_label = out["new_tree"][:8]
    if out["new"] == "staged":
        new_label += " (staged)"
    elif not out["new_tree"].startswith(out["new"]):
        new_label += " (%s)" % out["new"]
    lines = ["Move check: tree %s against %s" % (new_label, out["base"])]
    for old, new in out["renames"]:
        lines.append("Renamed: %s to %s, in %d lines" % (old, new, out["rename_counts"][old]))
    explain = 0
    for s in out["sources"]:
        left = s["gone_kinds"]
        explain += _explain(left) + _explain(s["came_kinds"])
        lines.append("Lines taken out of %s: %d" % (s["path"], s["taken"]))
        for unit, n in s["moved"]:
            lines.append("  in %s word for word and in order: %d" % (unit, n))
        lines.append("  the same but for `static` taken off: %d" % s["static_moved"])
        lines.append("  not in a unit as they were: %d%s" % (
            len(left), " (%s)" % _counts(left) if left else ""))
        lines.append("Lines added to %s: %d%s" % (s["path"], len(s["came"]),
                     " (%s)" % _counts(s["came_kinds"]) if s["came"] else ""))
    for u in out["units"]:
        rest = u["came_kinds"]
        explain += _explain(rest) + _explain(u["gone_kinds"])
        lines.append("Lines in %s not moved from %s: %d of %d%s" % (
            u["path"], " or ".join(u["from"]) or "a --from file", len(rest), u["size"],
            " (%s)" % _counts(rest) if rest else ""))
        if u["gone"]:
            lines.append("Lines taken out of %s: %d (%s)" % (u["path"], len(u["gone"]),
                                                            _counts(u["gone_kinds"])))
    if out["others"]:
        lines.append("Other files, listed below and not in the result:")
        for o in out["others"]:
            kinds = dict(o["came_kinds"])
            kinds.update({-n: k for n, k in o["gone_kinds"].items()})
            lines.append("  %s (+%d -%d%s)" % (o["path"], len(o["came"]), len(o["gone"]),
                                               ": %s" % _counts(kinds) if kinds else ""))
    lines.append("Result: " + ("every line accounted for" if not explain
                               else "%d lines to explain (marked ! below)" % explain))
    if out["static"]:
        lines += ["", "== `static` taken off, on the way or in place (%d):" % len(out["static"])]
        lines += ["  %s:%d  %s" % (u, no, t) for u, no, t in out["static"]]
    for s in out["sources"]:
        if s["gone"]:
            lines += ["", "== Lines taken out of %s and not in a unit as they were (%d):"
                      % (s["path"], len(s["gone"]))]
            lines += _listing("-", s["gone"], s["gone_kinds"])
        if s["came"]:
            lines += ["", "== Lines added to %s (%d):" % (s["path"], len(s["came"]))]
            lines += _listing("+", s["came"], s["came_kinds"])
    for u in out["units"]:
        if u["came"]:
            lines += ["", "== Lines in %s not moved (%d of %d):"
                      % (u["path"], len(u["came"]), u["size"])]
            lines += _listing("+", u["came"], u["came_kinds"])
        if u["gone"]:
            lines += ["", "== Lines taken out of %s (%d):" % (u["path"], len(u["gone"]))]
            lines += _listing("-", u["gone"], u["gone_kinds"])
    for o in out["others"]:
        lines += ["", "== %s (+%d -%d), not in the result:"
                  % (o["path"], len(o["came"]), len(o["gone"]))]
        lines += _listing("-", o["gone"], o["gone_kinds"])
        lines += _listing("+", o["came"], o["came_kinds"])
    return "\n".join(lines), explain


def _rename(text):
    old, sep, new = text.partition("=")
    if not sep or not NAME.match(old) or not NAME.match(new):
        raise argparse.ArgumentTypeError("a rename is OLD=NEW, two names: %r" % text)
    return old, new


def main(argv):
    parser = argparse.ArgumentParser(
        prog="move_check.py",
        usage="%(prog)s BASE [NEW] --from FILE [FILE ...] --to UNIT [UNIT ...] [--rename OLD=NEW ...]",
        description="Whether code moved to another file arrived as it left.")
    parser.add_argument("base")
    parser.add_argument("new", nargs="?")
    parser.add_argument("--from", dest="sources", action="extend", nargs="+", required=True)
    parser.add_argument("--to", dest="units", action="extend", nargs="+", required=True)
    parser.add_argument("--rename", action="append", default=[], type=_rename)
    try:
        args = parser.parse_args(argv[1:])
    except SystemExit as e:
        return 2 if e.code else 0
    try:
        out = check(args.base, args.new, args.sources, args.units, args.rename)
    except CheckError as e:
        print("move_check: %s" % e, file=sys.stderr)
        return 2
    text, explain = report(out)
    print(text)
    return 1 if explain else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
