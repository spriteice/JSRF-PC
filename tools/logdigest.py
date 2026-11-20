#!/usr/bin/env python3
"""Read JSRF-PC logs into a digest meant to be read whole.

    python3 tools/logdigest.py LOG [LOG ...] > digest.md
    python3 tools/logdigest.py LOG [LOG ...] --against OLD.log > digest.md
    python3 tools/logdigest.py --tag TAG LOG [LOG ...] [--against OLD.log]

Written for the project rules' Workflow 5 (docs/project-rules.md), which
reads a log over 100 KB through its digest and then reads the raw log where
the digest points, and kept here with tools/test_logdigest.py, which checks
its error lines against the lines the engine's source prints.

For each log, and in it each run (a run starts at the engine's banner,
`=== JSRF-PC <version> -- ... ===`, one per start, restarts included; the
lines before the first banner are a part of their own):

1. The run's header: the version, its system and Developer switches (read by
   tools/logsum.py's own parse, so the two tools agree), its first and last
   line, its size, and how it ended: a clean exit (the window closed, then
   the renderer's report), a crash report, a restart, or cut off, with a
   freeze report said beside it.
2. Every line counted by its tag (`[PACE]`, `[GL]`: the line's start after
   spaces; a line without one is "(no tag)"), with each tag's first and last
   line. The counts add up to the run's lines, and the digest says so.
3. Every error, check, crash and freeze line in full, with its line number
   and the line before and after it, grouped by ERRORS below. Past 100 lines
   in a group, its first 50 and last 50 with the count between. A crash
   report is given whole to the run's end (the process ends with it), up to
   200 lines and then its last 20; a freeze report, which repeats every 10 s
   while the freeze lasts, as its first and last, each with the 40 lines
   after its first.
4. The first of each kind of line, in full, with its line number and how
   many lines of its kind the run has. A kind is the line with its tag kept,
   every number made `#` (inside words too, with its sign and decimals) and
   a list of numbers one `#`, every word with a `/` or `\\` in it made
   `<path>`, and its spaces made one. The first 20 kinds of each tag; past
   that, how many more kinds and lines, and `--tag` for them. A later run
   of the same log lists only the kinds no earlier run had.
5. Across the logs, when there are several: the tags and kinds found in one
   log and not the others. With --against OLD.log: the tags and kinds this
   build's logs have that OLD.log doesn't (new), and those it had that they
   don't (gone).

--tag TAG lists every kind of TAG's lines with its count and first line,
then every error line of that tag in full; `--tag none` is the lines with no
tag. Nothing is left out of the digest without a line saying so.

A log is read as bytes and decoded as UTF-8 with replacement, line by line
at LF, a CR before it dropped, so a Windows log and one cut off mid-line read
the same, and the line numbers are those `sed -n` gives. A line over 2000
characters is shown as its first 2000 with how many more. Standard library
only; it writes nothing but its output.
"""
import argparse
import collections
import re
import sys

try:
    from tools import logsum            # imported, as the tests do
except ImportError:
    import logsum                       # run as python3 tools/logdigest.py

BANNER = logsum.HEADER                  # the run's banner, as logsum reads it
TAG = re.compile(r"^\s*\[([A-Z][A-Z0-9_]*)\]")
NO_TAG = "(no tag)"
# A kind: paths, then numbers (inside words too, a sign only where it starts
# a word), then spaces.
PATHS = re.compile(r"[^\s|'\"(),;]*[\\/][^\s|'\"(),;]*")
NUMBERS = re.compile(r"(?:(?<![\w.])[-+])?(?:0[xX][0-9A-Fa-f]+|\b[0-9A-Fa-f]*[0-9][0-9A-Fa-f]*\b|[0-9]+)"
                     r"(?:\.[0-9]+)?")
LISTS = re.compile(r"#(?:,? #)+")          # numbers in a list, after NUMBERS
SPACES = re.compile(r"\s+")

KINDS_A_TAG = 20                        # kinds of each tag in the digest
GROUP_LINES = 100                       # an error group's lines in full ...
GROUP_ENDS = 50                         # ... and past that, its first and last
CRASH_LINES = 200                       # a crash report's lines, then ...
CRASH_TAIL = 20                         # ... its last
FREEZE_LINES = 40                       # a freeze report: its first line and those after
LONG_LINE = 2000                        # characters of a line shown

# The lines the endings are told by (tools/test_logdigest.py finds each in
# the engine's source).
CLEAN_EXIT = "[GL] window closed; stopping"
RESTART = "[WIN] restart chosen; restarting"
CRASH_START = re.compile(r"^\[CRASH\] ")
FREEZE_START = re.compile(r"^\[FROZEN\] nothing has been presented")

# Error, check, crash and freeze lines: (group, pattern), matched by search
# against the line without its leading and trailing spaces. The first group
# that matches takes the line. tools/test_logdigest.py checks that each is
# printed by the engine, and that every line the engine prints with an error
# word (fail, error, cannot, refused, crash, frozen, fatal, corrupt, bad ...)
# is here or in NOT_ERRORS, and not in both.
ERRORS = (
    ("Crash report", r"^\[CRASH\] "),
    ("Freeze report", r"^\[FROZEN\] "),
    ("A check failed (RECOMP_CHECK)", r"^\[CHECK\] "),
    ("No exit (the watchdog)", r"^\[WATCHDOG\] "),
    ("Every guest thread stalled", r"^\[STALL\] "),
    ("A fatal path reached", r"^\[FATALX\] "),
    ("An untranslated instruction reached", r"^\[UNIMPL\] "),
    ("The heap's rules broken", r"^\[HEAPCHK\] "),
    ("The heap guard's findings", r"^\[HEAPGUARD\] (?!on:|off:|\d+ releases: )"),
    ("A heap allocation that can't be served",
     r"^xbox_HeapAlloc: cannot serve |^request exceeds the whole heap"),
    ("Memory refused", r"^\[KERNEL\] MmAllocateSystemMemory: .* REFUSED|^\[CONTIG\] refused "),
    ("A bad pointer from the game", r"^\[KERNEL\] .* refusing the store"),
    ("A kernel call without its argument size", r"^\[KERNEL\] .* has no entry in stdcall_args_for_ordinal"),
    ("A bad kernel slot", r"^\[KERNEL\] bad slot "),
    ("A thread not suspended", r"^\[SUSPEND\] .* failed"),
    ("A file not found on the disc", r"^\[FILE\] open D:\\.* FAILED"),
    ("A surface refused: it overlaps the program's image", r"^\[GPU\] REFUSING "),
    ("The memory layout", r"^xbox_MemoryLayoutInit: .*failed|^WARNING: |^Mirror \d+: FAILED"),
    ("The window, the GL context or a shader",
     r"^\[GL\] (?:SDL_Init failed|SDL_CreateWindow failed|cannot create |.* failed to compile|.*: link failed"
     r"|glGetError )"),
    ("Multisampling refused", r"^\[GL\] \d+ samples refused "),
    ("No sound device", r"^\[APU\] (?:.* failed|Failed )"),
    ("The shader cache", r"^\[SHADERS\] (?:cannot write|could not remove) "),
    ("A restart that couldn't relaunch", r"^\[WIN\] could not relaunch"),
    ("A start refused (Developer > Missions, JSRF_START_*)", r"^\[MISSION\] .*(?:refused|couldn't restart)"),
    ("A recording or playback not started (Developer > Playback, --play)",
     r"^\[PLAYBACK\] (?:(?:Record|Play): the game couldn't restart|can't (?:write|read) )"),
    ("An indirect call not resolved", r"^\[ICALL\] Failed to resolve "),
    ("The sound library's errors", r"^\[(?:ADXERR|ADXTRAP|ADXSTALL)\] |^\[ADXREC\] t="),
    ("A model's tree or bones corrupt", r"^\[TREE\] bad node |^\[BONE\] .* corrupt"),
    ("Vertices that differ", r"^\[ZYX\] .* BAD |^\[VCVERIFY\] (?!\d+ draws checked, 0 differed)"),
    ("A record not written", r"^\[SNAP\] cannot write |^\[icall-feedback\] cannot write "),
    ("A callee-saved register changed", r"^\[ABI\] "),
    ("A protected page written", r"^\[PROTECT\] write via "),
    ("The program not loaded", r"^Cannot open XBE|^Failed to (?:load|initialize) "),
    # Not the engine's: the loader's own (on the Ally X, Steam's overlay
    # library can't be preloaded into the 64-bit engine, which is harmless).
    ("Outside the engine: the loader's errors", r"^ERROR: "),
)
OUTSIDE_THE_ENGINE = {"Outside the engine: the loader's errors"}

# Lines with an error word that aren't errors, each with why.
NOT_ERRORS = (
    ("A failed open the game expects: its cache (Z:) or a save not made yet (U:), before the disc",
     r"^\[FILE\] open (?!D:\\).* FAILED|^\[FILE\] -> \S+ FAILED "),
    ("[GL]'s count of shader failures", r"^\[GL\] skipped: .* shader failures"),
    ("[UPSCALE]'s counts", r"^\[UPSCALE\] .* failed, "),
    ("A graffiti soul's course, the game's own", r"^\[SOUL\] .*fail"),
    ("A reservation clamped to what the heap holds", r"^\[KERNEL\] NtAllocateVirtualMemory: reserve .* cannot back "),
    ("A buffer too small, which the call reports as asked", r"^\[KERNEL\] NtQuerySymbolicLinkObject: .* cannot hold "),
    ("A vertex check's detail", r"^batch uses source indices "),
    ("[ADXT]'s register named err", r"^\[ADXT\] .* err\["),
)

_ERRORS = tuple((name, re.compile(p)) for name, p in ERRORS)
_NOT_ERRORS = tuple((name, re.compile(p)) for name, p in NOT_ERRORS)


def error_group(stripped):
    """The ERRORS group a line belongs to, or None."""
    for name, rx in _ERRORS:
        if rx.search(stripped):
            return name
    return None


def not_error(stripped):
    """The NOT_ERRORS entry a line matches, or None."""
    for name, rx in _NOT_ERRORS:
        if rx.search(stripped):
            return name
    return None


def tag_of(line):
    m = TAG.match(line)
    return m.group(1) if m else NO_TAG


def kind_of(line):
    """The line with its tag kept, and its paths, numbers, lists of numbers
    and runs of spaces each made one."""
    s = line.strip()
    m = TAG.match(s)
    head = s[:m.end()] if m else ""
    rest = PATHS.sub("<path>", s[len(head):])
    return head + LISTS.sub("#", SPACES.sub(" ", NUMBERS.sub("#", rest)))


def read_lines(path):
    """(number, text, bytes) for each line of the log: LF ends a line, a CR
    before it is dropped, bytes that aren't UTF-8 are replaced, and a last
    line with no LF is a line. The bytes are the line's own, its end too."""
    with open(path, "rb") as f:
        n = 0
        for raw in f:
            n += 1
            size = len(raw)
            if raw.endswith(b"\n"):
                raw = raw[:-1]
            if raw.endswith(b"\r"):
                raw = raw[:-1]
            yield n, raw.decode("utf-8", errors="replace"), size


class Group:
    """One error group's lines, each with the line before and after it: all of
    them up to GROUP_LINES, and past that the first and last GROUP_ENDS."""

    def __init__(self):
        self.count = 0
        self.first = []                 # [number, before, line, after]
        self.last = collections.deque(maxlen=GROUP_ENDS)

    def add(self, n, before, line):
        self.count += 1
        entry = [n, before, line, None]
        if len(self.first) < GROUP_LINES:
            self.first.append(entry)
        else:
            self.last.append(entry)
        return entry

    def entries(self):
        """The lines to show, and how many are left out between them."""
        if self.count <= GROUP_LINES:
            return self.first, [], 0
        return self.first[:GROUP_ENDS], list(self.last), self.count - 2 * GROUP_ENDS


class Block:
    """A report given whole: its line before, then its lines up to a cap,
    then the last few."""

    def __init__(self, n, before, cap, tail):
        self.start = n
        self.before = before
        self.lines = []
        self.cap = cap
        self.tail = collections.deque(maxlen=tail) if tail else None
        self.count = 0
        self.open = True

    def add(self, n, line):
        if len(self.lines) < self.cap:
            self.lines.append((n, line))
        elif self.tail is not None:
            self.tail.append((n, line))
        else:
            self.open = False           # full, with no tail: the report is shown
            return
        self.count += 1


class Part:
    """A run, or the lines before the log's first run."""

    def __init__(self, version, first):
        self.version = version          # None: the lines before the first run
        self.first = first
        self.last = first - 1
        self.bytes = 0
        self.first_line = None
        self.last_line = ""
        self.tags = {}                  # tag: [lines, first, last]
        self.kinds = {}                 # tag: {kind: [first line, its text, count]}
        self.groups = {}                # error group: Group
        self.crash = None               # Block
        self.freezes = 0
        self.freeze_first = None        # Block
        self.freeze_last = None         # Block
        self.clean = None               # the line the window closed at
        self.restart = None             # the line a restart was chosen at
        self.info = None                # logsum's run, for the header

    def ending(self):
        events = [(self.clean, "a clean exit: the window closed at line {}, then the renderer's report"),
                  (self.restart, "a restart, chosen at line {}"),
                  (self.crash and self.crash.start, "a crash report, from line {}")]
        events = [(n, words) for n, words in events if n]
        if events:
            n, words = max(events)
            said = words.format(n)
        else:
            said = "cut off, with no clean exit, crash report or restart before its last line"
        if self.freezes:
            said += (f"; a freeze report at line {self.freeze_first.start}"
                     + (f", {self.freezes} in all" if self.freezes > 1 else ""))
        return said


def shown(line):
    if len(line) <= LONG_LINE:
        return line
    return f"{line[:LONG_LINE]} ... ({len(line) - LONG_LINE} characters more)"


def digest_log(path):
    """The log's parts (the lines before its first run, if any, then each run),
    its line count and its size."""
    parts = []
    part = Part(None, 1)
    before = None                       # the line before this one
    waiting = []                        # entries still waiting for their line after
    n = size = 0
    for n, line, nbytes in read_lines(path):
        size += nbytes
        for entry in waiting:
            entry[3] = line
        waiting = []
        if BANNER.match(line):
            if part.last >= part.first:
                parts.append(part)
            part = Part(BANNER.match(line).group(1), n)
            before = None
        part.last = n
        part.bytes += nbytes
        if part.first_line is None:
            part.first_line = line
        part.last_line = line
        tag = tag_of(line)
        rec = part.tags.get(tag)
        if rec is None:
            part.tags[tag] = [1, n, n]
        else:
            rec[0] += 1
            rec[2] = n
        kinds = part.kinds.setdefault(tag, {})
        k = kind_of(line)
        e = kinds.get(k)
        if e is None:
            kinds[k] = [n, line, 1]
        else:
            e[2] += 1
        s = line.strip()
        for block in (part.crash, part.freeze_last):
            if block is not None and block.open:
                block.add(n, line)
        if s == CLEAN_EXIT:
            part.clean = n
        elif s == RESTART:
            part.restart = n
        group = error_group(s)
        if group is not None:
            if CRASH_START.match(s) and part.crash is None:
                part.crash = Block(n, before, CRASH_LINES, CRASH_TAIL)
                part.crash.add(n, line)
            elif FREEZE_START.match(s):
                part.freezes += 1
                part.freeze_last = Block(n, before, FREEZE_LINES + 1, 0)
                part.freeze_last.add(n, line)
                if part.freeze_first is None:
                    part.freeze_first = part.freeze_last
            elif group not in ("Crash report", "Freeze report"):
                waiting.append(part.groups.setdefault(group, Group()).add(n, before, line))
        before = line
    if part.last >= part.first:
        parts.append(part)
    runs = [p for p in parts if p.version is not None]
    for run, info in zip(runs, logsum.parse(path)):
        run.info = info
    return parts, n, size


def code(lines):
    return ["```"] + lines + ["```", ""]


def with_context(entries):
    """Lines with the line before and after each, each shown once."""
    out, last_shown = [], 0
    for n, before, line, after in entries:
        if before is not None and n - 1 > last_shown:
            out.append(f"{n - 1}: {shown(before)}")
        if n > last_shown:
            out.append(f"{n}: {shown(line)}")
        if after is not None:
            out.append(f"{n + 1}: {shown(after)}")
            last_shown = n + 1
        else:
            last_shown = n
    return out


def block_lines(block):
    out = []
    if block.before is not None:
        out.append(f"{block.start - 1}: {shown(block.before)}")
    out += [f"{n}: {shown(line)}" for n, line in block.lines]
    if block.tail:
        left = block.count - len(block.lines) - len(block.tail)
        if left:
            out.append(f"... {left} lines of the report left out ...")
        out += [f"{n}: {shown(line)}" for n, line in block.tail]
    return out


def part_title(part, i):
    if part.version is None:
        return f"Before the first run: lines {part.first} to {part.last}"
    return f"Run {i}: {part.version}, lines {part.first} to {part.last}"


def report_part(part, i, seen):
    out = [f"### {part_title(part, i)}", ""]
    lines = part.last - part.first + 1
    out.append(f"- Size: {lines} lines, {part.bytes} bytes.")
    if part.info is not None:
        for s in part.info["info"]:
            out.append(f"- System: {s}")
        out.append(f"- Developer switches: {', '.join(part.info['dev']) if part.info['dev'] else 'none'}")
        for note in part.info["notes"]:
            out.append(f"- Note: {note}")
    out.append(f"- First line, {part.first}: `{shown(part.first_line)}`")
    out.append(f"- Last line, {part.last}: `{shown(part.last_line)}`")
    if part.version is not None:
        out.append(f"- Ended: {part.ending()}.")
    out.append("")

    out += ["#### Lines by tag", "", "| Tag | Lines | First | Last |", "|---|---:|---:|---:|"]
    total = 0
    for tag, (count, first, last) in sorted(part.tags.items(), key=lambda kv: (-kv[1][0], kv[1][1])):
        out.append(f"| {tag} | {count} | {first} | {last} |")
        total += count
    out.append(f"| All | {total} | {part.first} | {part.last} |")
    out.append("")
    if total == lines:
        out += [f"Every line counted: {total} by tag, of the part's {lines}.", ""]
    else:
        out += [f"**Not every line counted: {total} by tag, of the part's {lines}.**", ""]

    out += ["#### Errors, checks, crashes and freezes", ""]
    if not part.groups and part.crash is None and not part.freezes:
        out += ["None.", ""]
    if part.crash is not None:
        out += [f"**Crash report,** from line {part.crash.start} to the run's end, line {part.last} "
                f"({part.crash.count} lines):", ""]
        out += code(block_lines(part.crash))
    if part.freezes:
        out += [f"**Freeze report,** {part.freezes} in this run: the first"
                + (" and the last" if part.freezes > 1 else "") + ", each with the "
                f"{FREEZE_LINES} lines after its first:", ""]
        out += code(block_lines(part.freeze_first))
        if part.freezes > 1:
            out += code(block_lines(part.freeze_last))
    for name, group in part.groups.items():
        out += [f"**{name},** {group.count} line{'s' if group.count != 1 else ''}:", ""]
        first, last, left = group.entries()
        shown_lines = with_context(first)
        if left:
            shown_lines.append(f"... {left} more of these lines; --tag lists them all ...")
            shown_lines += with_context(last)
        out += code(shown_lines)

    title = "The first of each kind of line"
    if any(seen.values()):
        title += ", if no earlier run of this log had it"
    out += [f"#### {title}", ""]
    repeated = 0
    for tag in sorted(part.kinds, key=lambda t: part.tags[t][1]):
        kinds = part.kinds[tag]
        before = seen.setdefault(tag, set())
        new = sorted((e for k, e in kinds.items() if k not in before), key=lambda e: e[0])
        repeated += len(kinds) - len(new)
        before.update(kinds)
        if not new:
            continue
        out += [f"**{tag_label(tag)}:** {len(kinds)} kind{'s' if len(kinds) != 1 else ''}, "
                f"{part.tags[tag][0]} line{'s' if part.tags[tag][0] != 1 else ''}"
                + (f"; {len(new)} new" if len(new) != len(kinds) else ""), ""]
        rows = [f"{n} (x{count}): {shown(text)}" for n, text, count in new[:KINDS_A_TAG]]
        if len(new) > KINDS_A_TAG:
            rest = new[KINDS_A_TAG:]
            rows.append(f"... {len(rest)} more kinds, {sum(e[2] for e in rest)} lines: --tag {tag_arg(tag)}")
        out += code(rows)
    if repeated:
        out += [f"{repeated} kinds of line an earlier run of this log had are not listed again.", ""]
    return out


def tag_label(tag):
    return tag if tag == NO_TAG else f"[{tag}]"


def tag_arg(tag):
    return "none" if tag == NO_TAG else tag


def log_kinds(parts):
    """{tag: {kind: [first line, text, count]}} over the whole log."""
    out = {}
    for part in parts:
        for tag, kinds in part.kinds.items():
            mine = out.setdefault(tag, {})
            for k, (n, text, count) in kinds.items():
                if k in mine:
                    mine[k][2] += count
                else:
                    mine[k] = [n, text, count]
    return out


def differences(name, mine, others, word):
    """The tags and kinds in `mine` that none of `others` has, by tag."""
    out = []
    tags = [t for t in mine if not any(t in o for o in others)]
    if tags:
        out += [f"Tags {word}: {', '.join(tag_label(t) for t in tags)}.", ""]
    for tag in sorted(mine, key=lambda t: min(e[0] for e in mine[t].values())):
        new = sorted((e for k, e in mine[tag].items() if not any(k in o.get(tag, ()) for o in others)),
                     key=lambda e: e[0])
        if not new:
            continue
        out += [f"**{tag_label(tag)}:** {len(new)} kind{'s' if len(new) != 1 else ''} {word}", ""]
        rows = [f"{n} (x{count}): {shown(text)}" for n, text, count in new[:KINDS_A_TAG]]
        if len(new) > KINDS_A_TAG:
            rows.append(f"... {len(new) - KINDS_A_TAG} more: --tag {tag_arg(tag)} {name}")
        out += code(rows)
    return out or [f"None {word}.", ""]


def report(paths, against=None):
    out = ["# JSRF-PC log digest", "",
           "Made by `tools/logdigest.py`. Line numbers are each log's own.", ""]
    every = []
    for path in paths:
        parts, lines, size = digest_log(path)
        runs = sum(1 for p in parts if p.version is not None)
        out += [f"## {path}", "", f"{lines} lines, {size} bytes, {runs} run{'s' if runs != 1 else ''}.", ""]
        if not runs:
            out += ["No run in this log (no `=== JSRF-PC` line).", ""]
        seen = {}
        i = 0
        for part in parts:
            if part.version is not None:
                i += 1
            out += report_part(part, i, seen)
        every.append((path, log_kinds(parts)))
    if len(every) > 1:
        out += ["## Across the logs", ""]
        for k, (path, kinds) in enumerate(every):
            others = [o for j, (_, o) in enumerate(every) if j != k]
            out += [f"### Only in {path}", ""] + differences(path, kinds, others, "only in this log")
    if against is not None:
        old_parts, _, _ = digest_log(against)
        old = log_kinds(old_parts)
        mine = {}
        for _, kinds in every:
            for tag, ks in kinds.items():
                for k, e in ks.items():
                    mine.setdefault(tag, {}).setdefault(k, e)
        out += [f"## New since {against}", ""] + differences(paths[0], mine, [old], f"not in {against}")
        out += [f"## Gone since {against}", ""] + differences(against, old, [mine], "not in this build's logs")
    return "\n".join(out)


def report_tag(tag, paths, against=None):
    want = NO_TAG if tag == "none" else tag.strip("[]")
    old = None
    if against is not None:
        old = log_kinds(digest_log(against)[0]).get(want, {})
    out = [f"# Every kind of {tag_label(want)} line", ""]
    for path in paths:
        kinds = log_kinds(digest_log(path)[0]).get(want, {})
        out += [f"## {path}", "", f"{len(kinds)} kind{'s' if len(kinds) != 1 else ''}, "
                f"{sum(e[2] for e in kinds.values())} lines"
                + (f"; marked new where {against} hasn't it" if old is not None else "") + ".", ""]
        rows = []
        for k, (n, text, count) in sorted(kinds.items(), key=lambda kv: kv[1][0]):
            mark = " (new)" if old is not None and k not in old else ""
            rows.append(f"{n} (x{count}){mark}: {text}")
        out += code(rows)
        errors = [(n, line) for n, line, _ in read_lines(path)
                  if tag_of(line) == want and error_group(line.strip()) is not None]
        if errors:
            out += [f"Its error lines, all {len(errors)}:", ""]
            out += code([f"{n}: {line}" for n, line in errors])
    return "\n".join(out)


def main(argv):
    parser = argparse.ArgumentParser(prog="logdigest.py", description=__doc__.split("\n\n")[0],
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("logs", nargs="+", metavar="LOG")
    parser.add_argument("--against", metavar="OLD.log",
                        help="the tags and kinds of line new since OLD.log, and those gone")
    parser.add_argument("--tag", metavar="TAG", help="every kind of TAG's lines, and its error lines "
                        "(none: the lines with no tag)")
    args = parser.parse_args(argv[1:])
    print(report_tag(args.tag, args.logs, args.against) if args.tag is not None
          else report(args.logs, args.against))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
