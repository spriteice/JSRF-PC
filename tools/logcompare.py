#!/usr/bin/env python3
"""Hold one build's logs to a row's numbered checks, before against after.

    python3 tools/logcompare.py CHECKS AFTER.log [AFTER.log ...] [--before LOG ...]  > checks.md

Written for b3's acceptance checks, which are numbers: each check names its
log line, what it takes from the run, the "before" and the limit, and this
prints each check's before, its after, and PASS or FAIL. It sits beside
tools/logdigest.py, which finds new kinds of line; this finds numbers that
moved. The checks files are in tools/checks/, and tools/test_logcompare.py
holds every line they name to a line the engine prints.

A checks file is INI, one section a check, named by its number as the row
numbers it (`[1]`, `[12b.4]`), in the order they are printed. Comments start
with `#`. A `[DEFAULT]` section's keys go to every check. The keys:

- name: what the check is, as the row says it. Needed.
- line: the line it reads, as a template: its text as the engine prints it,
  `{name}` for a number taken from it, `{}` for a number not used, `...` for
  any text, and a run of spaces for any run of spaces. It is matched from
  the line's start, the spaces before it ignored, and the line may go on
  after it. A line a later build words differently gives each wording, a
  template a line. With measure, the `{name}` it takes; with one `{name}`,
  that one.
- measure: `report KEY` in place of line: logsum's value KEY (`late_pct`,
  `held`, `worst_ms`, `draws_per_frame` and the rest, REPORT_KEYS below)
  for each of the run's full 10 s reports, the first and last left out as
  logsum's finish line leaves them out.
- take: what it takes over the run: max, min, mean, sum, count, first,
  last, or a percentile, p95 (the nearest rank). Needed.
- skip: leave out the first N lines matched in each run (or reports).
- from, until: templates of the lines that open and close the part of each
  run counted, as one stage's [LOAD] line to the next. Without from, a run
  is counted from its start. Those lines aren't counted themselves, and a
  line that matches both opens: the game's second line for the same stage
  doesn't close it.
- device: linux or windows, read from the log as logsum reads it; a log of
  the other is shown as "-" and isn't counted.
- before: a number, or a range as `65..71`: today's figure, as the row's
  table gives it. Logs given with --before take its place, their range
  being the spread with no change to the code (five replays of a saved run,
  say); where they have no value, the file's is used, marked (file).
- limit: `<`, `<=`, `>`, `>=` or `=` and a number, or for all but `=`,
  `before`, `before + N`, `before - N` or `before * N`. Against a range,
  `before` is its edge on the limit's side: `<= before` is no higher than
  the highest. Needed.

Each log's runs (from the engine's banner, as logsum splits them) are taken
together, each with its own skip and from. A check whose line isn't in a
log has no value there, but for count, which is then 0. A check passes when
every after log of its device passes. The exit status is 0 when every check
measured passes, 1 when one fails, has no line or has no before its limit
needs, and 2 when it can't run. Standard library only.
"""
import argparse
import configparser
import math
import re
import sys

try:
    from tools import logsum            # imported, as the tests do
except ImportError:
    import logsum                       # run as python3 tools/logcompare.py

# logsum's report values a check can read: the numbers of logsum.values.
REPORT_KEYS = ("frames", "late", "late_pct", "held", "held30", "worst_ms", "draws_per_frame", "kwords_per_frame",
               "voices")
KEYS = {"name", "line", "measure", "take", "skip", "from", "until", "device", "before", "limit"}
DEVICES = ("linux", "windows")
NUMBER = r"[-+]?\d+(?:\.\d+)?"
TOKEN = re.compile(r"\{([^{}]*)\}|\.\.\.| +")
TAKES = re.compile(r"^(?:max|min|mean|sum|count|first|last|p(\d+(?:\.\d+)?))$")
BEFORE = re.compile(rf"^({NUMBER})(?:\s*\.\.\s*({NUMBER}))?$")
LIMIT = re.compile(rf"^(<=|>=|<|>|=)\s*(?:before(?:\s*([-+*])\s*({NUMBER}))?|({NUMBER}))$")
OPS = {"<": lambda a, b: a < b, "<=": lambda a, b: a <= b, ">": lambda a, b: a > b,
       ">=": lambda a, b: a >= b, "=": lambda a, b: a == b}

PASS, FAIL, NO_LINE, NO_BEFORE, OTHER = "PASS", "FAIL", "NO LINE", "NO BEFORE", "not this device"


class CheckError(Exception):
    """A checks file that can't be read: exit status 2."""


class Template:
    """A line as a check names it (the module's doc, line)."""

    def __init__(self, text):
        self.text = text
        out, self.names, i = [], [], 0
        for m in TOKEN.finditer(text):
            out.append(re.escape(text[i:m.start()]))
            if m.group(0) == "...":
                out.append(".*?")
            elif m.group(0).startswith("{"):
                name = m.group(1)
                if name and (not name.isidentifier() or name in self.names):
                    raise CheckError(f"{text!r}: {{{name}}} isn't a name, or comes twice")
                if name:
                    self.names.append(name)
                # A whole number, not the end of one or its decimals.
                out.append(rf"(?<![\d.])" + (f"(?P<{name}>{NUMBER})" if name else NUMBER) + r"(?!\d)")
            else:
                out.append(" +")
            i = m.end()
        out.append(re.escape(text[i:]))
        self.regex = re.compile("".join(out))
        # The text before the first number, gap or space: a cheap test first.
        first = TOKEN.search(text)
        self.prefix = text[:first.start()] if first else text

    def match(self, s):
        return self.regex.match(s) if s.startswith(self.prefix) else None


def templates(value):
    return [Template(t.strip()) for t in value.splitlines() if t.strip()]


class Check:
    """One section of a checks file, read: a CheckError says what is wrong
    with it."""

    def __init__(self, number, keys):
        self.number = number
        where = f"check [{number}]"
        unknown = set(keys) - KEYS
        if unknown:
            raise CheckError(f"{where}: no such key: {', '.join(sorted(unknown))}")
        for key in ("name", "take", "limit"):
            if not keys.get(key, "").strip():
                raise CheckError(f"{where}: it needs {key}")
        self.name = " ".join(keys["name"].split())
        measure = " ".join(keys.get("measure", "").split())
        self.report = None
        self.lines = templates(keys.get("line", ""))
        if measure.startswith("report "):
            self.report = measure[len("report "):]
            if self.report not in REPORT_KEYS:
                raise CheckError(f"{where}: logsum has no report value {self.report!r}")
            if self.lines:
                raise CheckError(f"{where}: a report value or a line, not both")
        elif not self.lines:
            raise CheckError(f"{where}: it needs a line, or measure = report KEY")
        else:
            named = self.lines[0].names
            if not measure and len(named) == 1:
                measure = named[0]
            if not measure:
                raise CheckError(f"{where}: which number of the line? measure = one of {named}")
            for t in self.lines:
                if measure not in t.names:
                    raise CheckError(f"{where}: {t.text!r} has no {{{measure}}}")
        self.measure = measure
        self.take = keys["take"].strip()
        m = TAKES.match(self.take)
        if not m or (m.group(1) and not 0 < float(m.group(1)) <= 100):
            raise CheckError(f"{where}: take {self.take!r}: max, min, mean, sum, count, first, last or pN")
        skip = keys.get("skip", "0").strip()
        if not skip.isdigit():
            raise CheckError(f"{where}: skip is a count")
        self.skip = int(skip)
        self.start = templates(keys.get("from", ""))
        self.end = templates(keys.get("until", ""))
        self.device = keys.get("device", "").strip() or None
        if self.device not in (None,) + DEVICES:
            raise CheckError(f"{where}: device is linux or windows")
        self.before = None
        if keys.get("before", "").strip():
            m = BEFORE.match(keys["before"].strip())
            if not m or (m.group(2) and float(m.group(2)) < float(m.group(1))):
                raise CheckError(f"{where}: before is a number or a range, low..high")
            self.before = (float(m.group(1)), float(m.group(2) or m.group(1)))
        self.limit_text = " ".join(keys["limit"].split())
        m = LIMIT.match(self.limit_text)
        if not m or (m.group(1) == "=" and m.group(4) is None):
            raise CheckError(f"{where}: limit {self.limit_text!r}: an operator and a number, or before")
        self.op, self.by, self.amount, self.fixed = m.group(1), m.group(2), m.group(3), m.group(4)

    def needs_before(self):
        return self.fixed is None

    def bound(self, before):
        """The limit's number: fixed, or from the before's edge on its side."""
        if self.fixed is not None:
            return float(self.fixed)
        edge = before[1] if self.op in ("<", "<=") else before[0]
        if self.by is None:
            return edge
        n = float(self.amount)
        return {"+": edge + n, "-": edge - n, "*": edge * n}[self.by]


def read_checks(path):
    parser = configparser.ConfigParser(interpolation=None, comment_prefixes=("#",), inline_comment_prefixes=None,
                                       empty_lines_in_values=False)
    try:
        with open(path, encoding="utf-8") as f:
            parser.read_file(f)
    except (OSError, configparser.Error) as e:
        raise CheckError(f"{path}: {e}") from None
    checks = [Check(n, dict(parser[n])) for n in parser.sections()]
    if not checks:
        raise CheckError(f"{path}: no check in it")
    return checks


class Value:
    """What a check took from one log: a number, or why none."""

    def __init__(self, number=None, why=None, where=""):
        self.number, self.why, self.where = number, why, where


def take(check, found):
    """The check's take over (line, value) pairs, and where it came from."""
    how = check.take
    if how == "count":
        span = f"lines {found[0][0]} to {found[-1][0]}" if found else "none matched"
        return Value(len(found), where=f"{len(found)} counted, {span}")
    if not found:
        return Value(why=NO_LINE, where="no line")
    if how in ("max", "min", "first", "last") or how.startswith("p"):
        if how == "first":
            line, v = found[0]
        elif how == "last":
            line, v = found[-1]
        elif how in ("max", "min"):
            line, v = (max if how == "max" else min)(found, key=lambda x: x[1])
        else:
            ranked = sorted(found, key=lambda x: x[1])
            line, v = ranked[max(1, math.ceil(float(how[1:]) / 100 * len(ranked))) - 1]
        return Value(v, where=f"line {line} ({how} of {len(found)})")
    total = sum(v for _, v in found)
    return Value(total / len(found) if how == "mean" else total,
                 where=f"{len(found)} values, lines {found[0][0]} to {found[-1][0]}")


def measure(path, checks):
    """Each check's Value for one log."""
    runs = logsum.parse(path)
    devices = ["windows" if r["windows_os"] else "linux" for r in runs]
    # Per check and run: the values found, and the spans counted (line numbers).
    found = [[[] for _ in runs] for _ in checks]
    spans = [[[] for _ in runs] for _ in checks]
    is_open = [False] * len(checks)
    run = -1
    with open(path, encoding="utf-8", errors="replace") as f:
        for n, raw in enumerate(f, 1):
            line = raw.rstrip("\r\n")
            if logsum.HEADER.match(line):
                run += 1
                for k, c in enumerate(checks):
                    is_open[k] = not c.start
                    if is_open[k]:
                        spans[k][run].append([n, None])
                continue
            if run < 0:
                continue
            s = line.lstrip()
            for k, c in enumerate(checks):
                if c.start and any(t.match(s) for t in c.start):
                    if not is_open[k]:
                        is_open[k] = True
                        spans[k][run].append([n, None])
                elif is_open[k] and c.end and any(t.match(s) for t in c.end):
                    is_open[k] = False
                    spans[k][run][-1][1] = n
                elif is_open[k] and c.lines:
                    for t in c.lines:
                        m = t.match(s)
                        if m:
                            found[k][run].append((n, float(m.group(c.measure))))
                            break
    out = []
    for k, c in enumerate(checks):
        mine = [i for i, d in enumerate(devices) if c.device in (None, d)]
        if not runs:
            out.append(Value(why=NO_LINE, where="no run in this log"))
            continue
        if not mine:
            out.append(Value(why=OTHER, where=f"no {c.device} run"))
            continue
        pooled = []
        for i in mine:
            if c.report:
                rows = [r for r in logsum.full_reports(runs[i])
                        if any(a <= r["line"] and (b is None or r["line"] < b) for a, b in spans[k][i])]
                got = [(r["line"], logsum.values(r)[c.report]) for r in rows[c.skip:]]
                pooled += [(line, v) for line, v in got if v is not None]
            else:
                pooled += found[k][i][c.skip:]
        out.append(take(c, pooled))
    return out


def show(v):
    """A number as the table gives it: to two places, without trailing 0s."""
    return f"{v:.2f}".rstrip("0").rstrip(".")


def show_span(span):
    return show(span[0]) if span[0] == span[1] else f"{show(span[0])} to {show(span[1])}"


def compare(checks, after, before):
    """Each check's row: before, after, limit and result. after and before are
    lists of (path, [Value per check])."""
    rows = []
    for k, c in enumerate(checks):
        got = [vals[k].number for _, vals in before if vals[k].number is not None]
        if got:
            span = (min(got), max(got))
            before_cell = show_span(span)
        elif c.before:
            span = c.before
            before_cell = show_span(span) + " (file)"
        else:
            span, before_cell = None, "-"
        values = [vals[k] for _, vals in after]
        cells = [show(v.number) if v.number is not None else ("-" if v.why == OTHER else "no line")
                 for v in values]
        mine = [v for v in values if v.why != OTHER]
        if not mine:
            limit_cell, result = c.limit_text, OTHER
        elif c.needs_before() and span is None:
            limit_cell, result = c.limit_text, NO_BEFORE
        else:
            bound = c.bound(span)
            limit_cell = f"{c.op} {show(bound)}"
            if c.fixed is None:
                limit_cell += f" ({c.limit_text[len(c.op):].strip()})"
            if any(v.number is not None and not OPS[c.op](v.number, bound) for v in mine):
                result = FAIL
            elif any(v.number is None for v in mine):
                result = NO_LINE
            else:
                result = PASS
        rows.append((c, before_cell, ", ".join(cells), limit_cell, result))
    return rows


def cell(text):
    return text.replace("|", "\\|")


def print_report(path, checks, after, before, rows):
    print(f"# Log comparison: {path}")
    print()
    print(f"Before: {len(before)} log{'s' * (len(before) != 1)}" if before else
          "Before: the checks file's figures")
    for i, (p, _) in enumerate(before, 1):
        print(f"- before {i}: {p}")
    print(f"After: {len(after)} log{'s' * (len(after) != 1)}")
    for i, (p, _) in enumerate(after, 1):
        print(f"- after {i}: {p}")
    print()
    print("| # | Check | Before | After | Limit | Result |")
    print("|---|---|---|---|---|---|")
    for c, b, a, lim, result in rows:
        print(f"| {cell(c.number)} | {cell(c.name)} | {b} | {a} | {cell(lim)} | {result} |")
    print()
    counts = {r: sum(1 for row in rows if row[4] == r) for r in (PASS, FAIL, NO_LINE, NO_BEFORE, OTHER)}
    words = {PASS: "passed", FAIL: "failed", NO_LINE: "with no line", NO_BEFORE: "with no before",
             OTHER: "not this device"}
    print(", ".join(f"{n} {words[r]}" for r, n in counts.items() if n) + ".")
    print()
    print("## Where each value came from")
    print()
    for k, c in enumerate(checks):
        print(f"- **{cell(c.number)}:** " + "; ".join(
            [f"before {i}: {vals[k].where}" for i, (_, vals) in enumerate(before, 1)]
            + [f"after {i}: {vals[k].where}" for i, (_, vals) in enumerate(after, 1)]))


def main(argv):
    p = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    p.add_argument("checks", help="the checks file")
    p.add_argument("after", nargs="+", help="the build's logs")
    p.add_argument("--before", nargs="+", default=[], metavar="LOG", help="the logs to compare with")
    args = p.parse_args(argv[1:])
    try:
        checks = read_checks(args.checks)
        before = [(path, measure(path, checks)) for path in args.before]
        after = [(path, measure(path, checks)) for path in args.after]
    except (CheckError, OSError) as e:
        print(f"logcompare: {e}", file=sys.stderr)
        return 2
    rows = compare(checks, after, before)
    print_report(args.checks, checks, after, before, rows)
    return 0 if all(r[4] in (PASS, OTHER) for r in rows) else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
