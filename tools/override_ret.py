#!/usr/bin/env python3
"""Check that every override in port/ does its function's `ret`.

    python3 tools/override_ret.py [FILE ...] [--list]

A hand-written override (`void sub_XXXXXXXX(void)` in port/recomp_manual.c)
takes the place of the game's whole function. The recompiled caller pushes
the return address and calls it through the manual table, and does nothing
to the guest stack afterwards (tools/recomp/lifter.py, `_lift_call`). So on
entry `g_esp` points at the return address, and every path out of the
override has to leave it where the game's own `ret` would: the return
address taken off, and the arguments too for a function that pops its own.
Known bug 87 was an override whose rare path returned with the return
address still there, and nothing in a build showed it: the runtime's own
check, `RECOMP_ABI_CHECK`, is compiled out.

Each override's paths are followed through its body, and through the
functions of the files read (port/*.c) that it calls, adding up how far
`g_esp` has moved from where it came in. A path does the `ret`:

- by running the game's body, `sub_X_gen()`, called directly or passed to
  a helper that calls it;
- by a call into another of the game's functions with `g_esp` where it
  came in, which does its own `ret` in the override's place
  (`sub_0003D5D0`'s text, drawn by `sub_0003D610`);
- or by hand: `g_esp += 4` (`4 + 8` for a function that pops two
  arguments), or `g_esp = esp + 4` from where it was kept on entry, or
  from a helper that gives back where it would be (`model_node`).

Moves in between are followed too: `g_esp -= n` for room on the guest
stack, `g_esp = esp` to put it back, and a call of the game's after a
pushed return address, which pops that return address. A helper's value
is followed where it is a number, as one that returns 1 when it has run
the body. Every other call is taken to leave `g_esp` as it found it. Each
path out must then have done the `ret` exactly once: the body's, or 4
bytes or more by hand, a multiple of 4. Anything else is listed with its
line: a return with no `ret` (bug 87's), a second `ret`, a move that isn't
one, or a path whose `g_esp` the check can't follow, which is listed rather
than passed: a `ret` inside a loop that can come round again is one. An
override whose paths pop different amounts by hand is listed too.

The overrides are the functions the recompiler leaves to port/: what
tools/recomp/manual_scan.py finds the file defines or wraps, those a macro
of the file defines among them (`MENU_NAV_WRAP`), and nothing inside
`#if 0`, which is read out by manual_scan's own rule. The exit status is 0
when every path of every override does its `ret`, 1 when something is
listed, and 2 when it can't run. `--list` also prints how each override's
paths do it. It needs only Python's standard library. Its test,
tools/test_override_ret.py, runs it on port/ in CI's tool tests, and on
made-up overrides, known bug 87's held path among them.
"""
import argparse
import ast
import pathlib
import re
import sys

if __package__ in (None, ""):
    sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
from tools.recomp.manual_scan import scan, strip_disabled  # noqa: E402

REPO = pathlib.Path(__file__).resolve().parents[1]
SP = "g_esp"                    # the guest stack pointer, in hand-written code
GUEST_FN = re.compile(r"sub_[0-9A-Fa-f]{8}(?:_gen)?")
OVERRIDE = re.compile(r"sub_([0-9A-Fa-f]{8})")
MAX_STATES = 64                 # paths kept apart at one point, before giving up on them
MAX_LOOP = 32                   # passes over a loop's body, before giving up on it
WIDEN = 8                       # passes before a move that keeps changing is given up on
MAX_DEPTH = 12                  # helpers called inside helpers, before giving up on them
TOP = "TOP"                     # a move of g_esp that can't be followed
KEYWORDS = {
    "if", "else", "while", "for", "do", "switch", "case", "default", "return",
    "break", "continue", "goto", "sizeof", "typedef", "struct", "union", "enum",
    "static", "extern", "const", "volatile", "inline", "register", "auto",
    "signed", "unsigned", "void", "char", "short", "int", "long", "float",
    "double", "_Bool", "__attribute__", "__declspec", "_Alignof", "alignof",
    "__typeof__", "typeof", "_Static_assert", "offsetof",
}
INT_TYPES = {"uint32_t", "int32_t", "unsigned", "int", "size_t", "uintptr_t"}
ASSIGN_OPS = {"=", "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "<<=", ">>="}
PUNCT = sorted("""
>>= <<= ... -> ++ -- << >> <= >= == != && || += -= *= /= %= &= ^= |= ##
{ } ( ) [ ] ; , : ? . + - * / % & | ^ ! ~ < > =
""".split(), key=len, reverse=True)
TOKEN = re.compile(r"""
    (?P<id>[A-Za-z_]\w*)
  | (?P<num>\.?\d(?:[eEpP][+-]|[\w.])*)
  | (?P<str>"[ ]*"|'[ ]*')
  | (?P<punct>""" + "|".join(re.escape(p) for p in PUNCT) + r""")
  | (?P<space>\s+)
  | (?P<other>.)
""", re.X)


class Unreadable(Exception):
    """Code the check can't follow, listed against its line."""

    def __init__(self, line, why):
        super().__init__(why)
        self.line, self.why = line, why


# ---------------------------------------------------------------------------
# Reading a file

def live_lines(text):
    """The file's lines, those `#if 0` leaves out made empty by manual_scan's
    own rule (strip_disabled), so the code read is the code the recompiler
    counts, each line at its own number. Each line is tagged with its number
    at its end for the rule, which reads only each line's start, and the
    tags on the lines it keeps say which they were."""
    lines = text.splitlines()
    tagged = "".join(f"{line}\x00{n}\x00\n" for n, line in enumerate(lines))
    out = [""] * len(lines)
    for kept in strip_disabled(tagged).splitlines():
        m = re.search(r"\x00(\d+)\x00$", kept)
        out[int(m.group(1))] = kept[:m.start()]
    return out


def blank(text):
    """Comments, and the insides of strings and characters, made spaces, with
    each newline kept, so nothing in them is read as code."""
    out, i, n = [], 0, len(text)
    while i < n:
        if text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append(re.sub(r"[^\n]", " ", text[i:j]))
            i = j
        elif text.startswith("//", i):
            j = text.find("\n", i)
            j = n if j < 0 else j
            out.append(" " * (j - i))
            i = j
        elif text[i] in "\"'":
            q, j = text[i], i + 1
            while j < n and text[j] not in (q, "\n"):
                j += 2 if text[j] == "\\" else 1
            out.append(q + " " * (min(j, n) - i - 1) + q)
            i = j + 1
        else:
            out.append(text[i])
            i += 1
    return "".join(out)


class Tok:
    __slots__ = ("kind", "text", "line")

    def __init__(self, kind, text, line):
        self.kind, self.text, self.line = kind, text, line

    def __repr__(self):
        return f"{self.text}@{self.line}"


def tokenize(text, first_line=1):
    """Tokens, a preprocessor line (with its \\ continuations) as one `pp`
    token."""
    toks, line, at_start, i, n = [], first_line, True, 0, len(text)
    while i < n:
        if at_start and text[i] == "#":
            e = i
            while True:
                e = text.find("\n", e)
                e = n if e < 0 else e
                if e < n and text[i:e].rstrip().endswith("\\"):
                    e += 1
                    continue
                break
            toks.append(Tok("pp", " ".join(text[i:e].replace("\\\n", " ").split()), line))
            line += text.count("\n", i, e)
            i, at_start = e, False
            continue
        m = TOKEN.match(text, i)
        if m.lastgroup == "space":
            if "\n" in m.group():
                line += m.group().count("\n")
                at_start = True
        else:
            toks.append(Tok(m.lastgroup, m.group(), line))
            at_start = False
        i = m.end()
    return toks


def is_p(tok, *texts):
    return tok.kind == "punct" and tok.text in texts


def matching(toks, i):
    """The index of the bracket that closes the one at toks[i]."""
    pairs = {"(": ")", "[": "]", "{": "}"}
    depth = 0
    for j in range(i, len(toks)):
        if is_p(toks[j], "(", "[", "{"):
            depth += 1
        elif is_p(toks[j], ")", "]", "}"):
            depth -= 1
            if depth == 0:
                if toks[j].text != pairs[toks[i].text]:
                    raise Unreadable(toks[j].line, f"'{toks[j].text}' closes '{toks[i].text}'")
                return j
    raise Unreadable(toks[i].line, f"nothing closes '{toks[i].text}'")


def split_top(toks, sep):
    """toks split at each `sep` outside brackets."""
    parts, cur, depth = [], [], 0
    for t in toks:
        if is_p(t, "(", "[", "{"):
            depth += 1
        elif is_p(t, ")", "]", "}"):
            depth -= 1
        if depth == 0 and is_p(t, sep):
            parts.append(cur)
            cur = []
        else:
            cur.append(t)
    parts.append(cur)
    return parts


def strip_parens(toks):
    """toks without the parentheses round all of them, or a cast to an
    integer type before them."""
    while True:
        if len(toks) >= 2 and is_p(toks[0], "(") and matching(toks, 0) == len(toks) - 1:
            toks = toks[1:-1]
        elif len(toks) > 3 and is_p(toks[0], "(") and is_p(toks[2], ")") and toks[1].text in INT_TYPES:
            toks = toks[3:]
        else:
            return toks


class Func:
    """A function a file defines: its parameters, its body's tokens, and
    whether it is static to its file."""

    def __init__(self, name, params, body, line, end_line, static):
        self.name, self.body, self.line, self.end_line = name, body, line, end_line
        self.params, self.static, self.source = params, static, None
        self._stmts = None

    def stmts(self):
        if self._stmts is None:
            self._stmts = Parser(self.body).block(0, len(self.body))
        return self._stmts


def params_of(toks):
    """Each parameter's name, None where it has none."""
    if [t.text for t in toks] in ([], ["void"]):
        return []
    out = []
    for part in split_top(toks, ","):
        texts = [t.text for t in part]
        fn = next((texts[k + 2] for k in range(len(texts) - 3)
                   if texts[k:k + 2] == ["(", "*"] and part[k + 2].kind == "id"), None)
        if fn:                          # void (*body)(void)
            out.append(fn)
            continue
        while part and is_p(part[-1], "]"):
            part = part[:part_open(part)]
        named = len(part) > 1 and part[-1].kind == "id" and part[-1].text not in KEYWORDS
        out.append(part[-1].text if named else None)
    return out


def part_open(toks):
    """The index of the [ that the last token, a ], closes."""
    depth = 0
    for j in range(len(toks) - 1, -1, -1):
        if is_p(toks[j], "]"):
            depth += 1
        elif is_p(toks[j], "["):
            depth -= 1
            if depth == 0:
                return j
    raise Unreadable(toks[-1].line, "an unmatched ]")


def pp_defines(toks):
    """(function-like macros as name -> (parameters, body), numeric macros as
    name -> value) of a file's tokens."""
    fmacros, consts = {}, {}
    for t in toks:
        if t.kind != "pp":
            continue
        m = re.match(r"#\s*define\s+(\w+)\(([^)]*)\)\s?(.*)$", t.text)
        if m:
            fmacros[m.group(1)] = ([p.strip() for p in m.group(2).split(",") if p.strip()], m.group(3))
            continue
        m = re.match(r"#\s*define\s+(\w+)\s+(.+)$", t.text)
        if m:
            v = const_value(tokenize(m.group(2)), consts)
            if v is not None:
                consts[m.group(1)] = v
    return fmacros, consts


def expand(params, body, args):
    """A macro's body with its arguments in place, and ## pasted."""
    vals = dict(zip(params, args))
    out = re.sub(r"\b\w+\b", lambda m: vals.get(m.group(), m.group()), body)
    return re.sub(r"\s*##\s*", "", out)


def functions(toks, fmacros):
    """name -> [Func], for each function defined at the file's top level,
    and in the expansions of its function-like macros used there."""
    out, i, n, start = {}, 0, len(toks), 0
    while i < n:
        t = toks[i]
        if t.kind == "pp" or is_p(t, ";", "}"):
            i += 1
            start = i
        elif t.kind == "id" and t.text in fmacros and i == start and i + 1 < n and is_p(toks[i + 1], "("):
            close = matching(toks, i + 1)
            args = [" ".join(a.text for a in part) for part in split_top(toks[i + 2:close], ",")]
            params, body = fmacros[t.text]
            inner = tokenize(expand(params, body, args))
            for k in inner:
                k.line = t.line
            for name, fs in functions(inner, {}).items():
                out.setdefault(name, []).extend(fs)
            i = start = close + 1
        elif is_p(t, "{"):
            close = matching(toks, i)
            head = toks[start:i]
            if head and is_p(head[-1], ")") and not any(is_p(x, "=") for x in head):
                j = len(head) - 1
                depth = 0
                while j >= 0:
                    if is_p(head[j], ")"):
                        depth += 1
                    elif is_p(head[j], "("):
                        depth -= 1
                        if depth == 0:
                            break
                    j -= 1
                if j >= 1 and head[j - 1].kind == "id" and head[j - 1].text not in KEYWORDS:
                    name = head[j - 1].text
                    out.setdefault(name, []).append(
                        Func(name, params_of(head[j + 1:-1]), toks[i + 1:close], head[j - 1].line,
                             toks[close].line, any(x.text == "static" for x in head[:j - 1])))
            i = close + 1
            if i < n and not is_p(toks[i], ";"):
                start = i
        else:
            i += 1
    return out


# ---------------------------------------------------------------------------
# Statements

class Parser:
    """A body as nested statements: ("block", [..]), ("if", cond, then, else),
    ("loop", how, init, cond, step, body, line), ("switch", cond, body),
    ("label", "case" or "default", line), ("return", expr, line),
    ("break",), ("continue",), ("expr", toks), and ("ppif", [branches]) for a
    preprocessor conditional round whole statements."""

    def __init__(self, toks):
        self.t = toks

    def text(self, i):
        return self.t[i].text if i < len(self.t) else None

    def line(self, i):
        return self.t[min(i, len(self.t) - 1)].line if self.t else 0

    def until(self, i, end, stop):
        """The index of the first `stop` outside brackets, from i."""
        depth = 0
        for j in range(i, end):
            if is_p(self.t[j], "(", "[", "{"):
                depth += 1
            elif is_p(self.t[j], ")", "]", "}"):
                depth -= 1
            elif depth == 0 and is_p(self.t[j], stop):
                return j
        raise Unreadable(self.line(i), f"no '{stop}' where a statement ends")

    def block(self, i, end):
        items = []
        while i < end:
            node, i = self.stmt(i, end)
            if node is not None:
                items.append(node)
        return ("block", items)

    def paren(self, i):
        if self.text(i) != "(":
            raise Unreadable(self.line(i), "no '(' after if, while, for or switch")
        close = matching(self.t, i)
        return self.t[i + 1:close], close + 1

    def stmt(self, i, end):
        t, s = self.t[i], self.t[i].text
        if t.kind == "pp":
            return self.pp(i, end)
        if is_p(t, ";"):
            return None, i + 1
        if is_p(t, "{"):
            close = matching(self.t, i)
            return self.block(i + 1, close), close + 1
        if s == "if":
            cond, i = self.paren(i + 1)
            then, i = self.stmt(i, end)
            other = None
            if self.text(i) == "else":
                other, i = self.stmt(i + 1, end)
            return ("if", cond, then, other), i
        if s in ("while", "switch"):
            cond, i = self.paren(i + 1)
            body, i = self.stmt(i, end)
            return (("switch", cond, body) if s == "switch" else ("loop", "while", [], cond, [], body, t.line)), i
        if s == "for":
            head, i = self.paren(i + 1)
            parts = split_top(head, ";")
            if len(parts) != 3:
                raise Unreadable(t.line, "a for without its two ';'")
            body, i = self.stmt(i, end)
            return ("loop", "for", parts[0], parts[1], parts[2], body, t.line), i
        if s == "do":
            body, i = self.stmt(i + 1, end)
            if self.text(i) != "while":
                raise Unreadable(t.line, "a do without its while")
            cond, i = self.paren(i + 1)
            return ("loop", "do", [], cond, [], body, t.line), self.until(i, end, ";") + 1
        if s == "case":
            depth = 0
            for j in range(i + 1, end):
                if is_p(self.t[j], "?"):
                    depth += 1
                elif is_p(self.t[j], ":"):
                    if depth == 0:
                        return ("label", s, t.line), j + 1
                    depth -= 1
            raise Unreadable(t.line, "a case with no ':'")
        if s == "default":
            return ("label", s, t.line), self.until(i, end, ":") + 1
        if s == "return":
            j = self.until(i, end, ";")
            return ("return", self.t[i + 1:j], t.line), j + 1
        if s in ("break", "continue"):
            return (s,), self.until(i, end, ";") + 1
        if s == "goto":
            raise Unreadable(t.line, "a goto")
        if t.kind == "id" and s not in KEYWORDS and self.text(i + 1) == ":":
            return None, i + 2          # a label, which no goto reaches
        j = self.until(i, end, ";")
        return ("expr", self.t[i:j]), j + 1

    def pp(self, i, end):
        """#if round whole statements: each branch is a path. #define and the
        other directives change no path."""
        def directive(k):
            return re.match(r"#\s*(\w*)", self.t[k].text).group(1) if self.t[k].kind == "pp" else None
        if directive(i) not in ("if", "ifdef", "ifndef"):
            if directive(i) in ("elif", "else", "endif"):
                raise Unreadable(self.line(i), f"#{directive(i)} with no #if in the same block")
            return None, i + 1
        branches, cur, depth, has_else = [], [], 0, False
        for j in range(i + 1, end):
            d = directive(j)
            if d in ("if", "ifdef", "ifndef"):
                depth += 1
            elif d in ("elif", "else", "endif") and depth == 0:
                branches.append(cur)
                cur = []
                has_else |= d == "else"
                if d == "endif":
                    if not has_else:
                        branches.append([])
                    return ("ppif", [Parser(b).block(0, len(b)) for b in branches]), j + 1
                continue
            elif d == "endif":
                depth -= 1
            cur.append(self.t[j])
        raise Unreadable(self.line(i), "#if with no #endif in the same block")


# ---------------------------------------------------------------------------
# Where g_esp is
#
# A path's state is (move, kept). The move is how far g_esp has gone from
# where the override came in, as (bytes, rets): the bytes the code itself
# moved it, and how many times a function of the game's did a `ret` in the
# override's place (each of the override's own size, which isn't known
# here), or TOP. kept holds the locals that hold g_esp, or g_esp plus a
# number, each with the move it holds: ((name, move), ...).

def add(move, k):
    return TOP if move == TOP or k is None else (move[0] + k, move[1])


def const_value(toks, consts):
    """A number made of literals and the file's numeric macros, or None."""
    s = []
    for k, t in enumerate(toks):
        if t.kind == "num":
            v = re.sub(r"[uUlL]+$", "", t.text)
            if not re.fullmatch(r"0[xX][0-9A-Fa-f]+|\d+", v):
                return None
            s.append(str(int(v, 0)))
        elif t.kind == "id" and t.text in consts:
            s.append(str(consts[t.text]))
        elif t.kind == "id" and t.text in INT_TYPES and k and is_p(toks[k - 1], "(") \
                and k + 1 < len(toks) and is_p(toks[k + 1], ")"):
            s[-1] = ""                  # a cast, (uint32_t)
            s.append("#")
        elif t.kind == "punct" and t.text in ("+", "-", "*", "(", ")", "<<", ">>", "|", "&", "~"):
            if t.text == ")" and s and s[-1] == "#":
                s[-1] = ""
                continue
            s.append(t.text)
        else:
            return None
    try:
        tree = ast.parse(" ".join(x for x in s if x), mode="eval")
    except SyntaxError:
        return None
    allowed = (ast.Expression, ast.BinOp, ast.UnaryOp, ast.Constant, ast.Add, ast.Sub, ast.Mult,
               ast.LShift, ast.RShift, ast.BitOr, ast.BitAnd, ast.USub, ast.UAdd, ast.Invert)
    if not all(isinstance(node, allowed) for node in ast.walk(tree)):
        return None
    return eval(compile(tree, "<constant>", "eval"), {"__builtins__": {}})  # only the nodes above


def lvalue_key(toks):
    """The name an assignment's left side writes, a declaration's type taken
    off (`const uint32_t esp` is esp, `sv[4]` is sv[4]), or None."""
    j = len(toks) - 1
    while j >= 0 and is_p(toks[j], "]"):
        j = part_open(toks[:j + 1]) - 1
    while j >= 2 and toks[j].kind == "id" and is_p(toks[j - 1], ".", "->"):
        j -= 2
    if j < 0 or toks[j].kind != "id":
        return None
    return "".join(t.text for t in toks[j:])


class Walk:
    """One override's paths, followed through the functions it calls."""

    def __init__(self, code, root):
        self.code, self.root = code, root
        self.stack, self.cache, self.exits, self.src = [], {}, None, None

    @staticmethod
    def norm(states):
        """states, or once there are too many, each with its move given up."""
        if len(states) <= MAX_STATES:
            return states
        states = {(TOP, kept) for _, kept in states}
        return states if len(states) <= MAX_STATES else {(TOP, ())}

    def const(self, toks):
        return const_value(toks, self.src.consts)

    def form(self, toks, state):
        """The move an expression holds when it is g_esp, or a kept local,
        plus or minus a number; None for anything else."""
        terms, sign, cur, depth = [], 1, [], 0
        for t in strip_parens(toks):
            if is_p(t, "(", "["):
                depth += 1
            elif is_p(t, ")", "]"):
                depth -= 1
            if depth == 0 and is_p(t, "+", "-") and cur:
                terms.append((sign, cur))
                sign, cur = (1 if t.text == "+" else -1), []
            else:
                cur.append(t)
        terms.append((sign, cur))
        base, off, kept = None, 0, dict(state[1])
        for sgn, term in terms:
            term = strip_parens(term)
            key = "".join(t.text for t in term)
            if key == SP or key in kept:
                if base is not None or sgn < 0:
                    return None
                base = state[0] if key == SP else kept[key]
                continue
            v = self.const(term)
            if v is None:
                return None
            off += sgn * v
        return None if base is None else add(base, off)

    def value(self, toks, state):
        """What a function returns: the move it holds, a number, or None."""
        if not toks:
            return None
        v = self.form(toks, state)
        return self.const(strip_parens(toks)) if v is None else v

    def assign(self, key, op, rhs, pairs):
        """The states after `key op rhs`, from (state, the value rhs's call
        gave, or None) pairs."""
        out = set()
        for state, value in pairs:
            move, kept = state[0], dict(state[1])
            if key == SP:
                if op == "=":
                    v = self.form(rhs, state) if value is None else value
                    move = v if isinstance(v, tuple) or v == TOP else TOP
                elif op in ("+=", "-="):
                    k = self.const(strip_parens(rhs))
                    move = add(move, None if k is None else (k if op == "+=" else -k))
                else:
                    move = TOP
            else:
                if op == "=":
                    v = self.form(rhs, state) if value is None else value
                elif op in ("+=", "-=") and key in kept:
                    k = self.const(strip_parens(rhs))
                    v = add(kept[key], None if k is None else (k if op == "+=" else -k))
                else:
                    v = None
                if not (isinstance(v, tuple) or v == TOP):
                    kept.pop(key, None)
                else:
                    kept[key] = v
            out.add((move, tuple(sorted(kept.items()))))
        return self.norm(out)

    def fn(self, name, env):
        """The function a name stands for here: one a parameter or local was
        given, or one of the game's or of the code read; None for anything
        else."""
        name = env.get(name, name)
        return name if GUEST_FN.fullmatch(name) or self.code.lookup(name, self.src) else None

    # -- expressions -------------------------------------------------------

    def expr(self, toks, states, env, maybe=False):
        for part in split_top(toks, ","):
            states = self.part(part, states, env, maybe)
        return states

    def part(self, toks, states, env, maybe):
        """An expression with no comma outside brackets: an assignment's right
        side, then its write; otherwise its calls, in order."""
        depth = 0
        for k, t in enumerate(toks):
            if is_p(t, "(", "[", "{"):
                depth += 1
            elif is_p(t, ")", "]", "}"):
                depth -= 1
            elif depth == 0 and t.kind == "punct" and t.text in ASSIGN_OPS:
                lhs, rhs = strip_parens(toks[:k]), toks[k + 1:]
                if any(is_p(x, "?", "&&", "||") for x in lhs):
                    break
                key = lvalue_key(lhs)
                bound = strip_parens(rhs)
                if t.text == "=" and key is not None and self.whole_call(bound):
                    pairs = self.call_value(bound, states, env)
                else:
                    pairs = {(s, None) for s in self.part(rhs, states, env, maybe)}
                if key is None:         # a write to memory: *(p) = .., MEM32(a) = ..
                    return self.calls(lhs, {s for s, _ in pairs}, env, maybe)
                if t.text == "=" and len(bound) == 1 and bound[0].kind == "id" and self.fn(bound[0].text, env):
                    env[key] = self.fn(bound[0].text, env)
                else:
                    env.pop(key, None)
                after = self.assign(key, t.text, rhs, pairs)
                return self.norm(states | after) if maybe else after
        return self.calls(toks, states, env, maybe)

    def truth(self, toks, states, env):
        """(the states where a condition can be true, those where it can be
        false). A helper's value is known where it returns a number, as
        `return 1;` after running the body and `return 0;` before it, and
        &&, || and ! keep it."""
        true, false = set(), set()
        for state, value in self.cond(toks, states, env):
            if value is not False:
                true.add(state)
            if value is not True:
                false.add(state)
        return self.norm(true), self.norm(false)

    def cond(self, toks, states, env):
        """{(state, True, False or None for either)} after a condition."""
        toks = strip_parens(toks)
        for op in ("||", "&&"):
            parts = split_top(toks, op)
            if len(parts) > 1:
                pairs = self.cond(parts[0], states, env)
                for part in parts[1:]:
                    out = set()
                    for state, value in pairs:
                        # || skips the rest where it's true, && where it's false
                        if value is not None and value == (op == "||"):
                            out.add((state, value))
                            continue
                        if value is None:
                            out.add((state, op == "||"))
                        out |= self.cond(part, {state}, env)
                    pairs = out
                return pairs
        if toks and is_p(toks[0], "!") and (len(toks) == 2 or is_p(toks[1], "(") and
                                           matching(toks, 1) == len(toks) - 1 or self.whole_call(toks[1:])):
            return {(s, None if v is None else not v) for s, v in self.cond(toks[1:], states, env)}
        if self.whole_call(toks):
            return {(s, bool(v) if isinstance(v, int) else None) for s, v in self.call_value(toks, states, env)}
        return {(s, None) for s in self.expr(toks, states, env)}

    @staticmethod
    def whole_call(toks):
        return len(toks) >= 3 and toks[0].kind == "id" and toks[0].text not in KEYWORDS and \
            is_p(toks[1], "(") and matching(toks, 1) == len(toks) - 1

    def calls(self, toks, states, env, maybe):
        """The calls in toks, in order. After &&, || or ? each may not run."""
        i, n = 0, len(toks)
        while i < n:
            t = toks[i]
            if is_p(t, "&&", "||", "?"):
                maybe = True
            elif is_p(t, "(") and i + 4 < n and is_p(toks[i + 1], "*") and toks[i + 2].kind == "id" \
                    and is_p(toks[i + 3], ")") and is_p(toks[i + 4], "("):
                i, states = self.call_at(toks[i + 2], toks, i + 4, states, env, maybe)   # (*fn)(..)
            elif is_p(t, "(", "[", "{"):
                close = matching(toks, i)
                states = self.expr(toks[i + 1:close], states, env, maybe)
                i = close
            elif t.text == SP:
                before = toks[i - 1].text if i else None
                after = toks[i + 1].text if i + 1 < n else None
                if before == "&" and (i < 2 or toks[i - 2].kind == "punct" and toks[i - 2].text not in (")", "]")):
                    raise Unreadable(t.line, f"&{SP} taken")
                if "++" in (before, after) or "--" in (before, after):
                    raise Unreadable(t.line, f"{SP} moved by ++ or --")
            elif t.kind == "id" and t.text not in KEYWORDS and i + 1 < n and is_p(toks[i + 1], "(") \
                    and not (i and is_p(toks[i - 1], ".", "->")):
                i, states = self.call_at(t, toks, i + 1, states, env, maybe)
            i += 1
        return states

    def call_at(self, name, toks, open_at, states, env, maybe):
        """A call of name, its arguments from open_at: (the index of its
        closing parenthesis, the states after it)."""
        close = matching(toks, open_at)
        args = split_top(toks[open_at + 1:close], ",") if close > open_at + 1 else []
        for a in args:
            states = self.expr(a, states, env, maybe)
        after = {s for s, _ in self.call(name, args, states, env)}
        return close, (self.norm(states | after) if maybe else after)

    def guest_ret(self, name, move):
        """The move after a call of the game's body, or of another of the
        game's functions, which does its own `ret`. With g_esp where the
        override came in, that is the override's `ret` (a tail call), and the
        override's own body again is a second. Below it, the code pushed the
        call's return address (`g_esp -= 4`, written, then the call), and the
        callee's `ret` pops it: what the code pushed before that is its own to
        take off, as for a cdecl call. Anywhere else what it pops isn't known."""
        if move == TOP:
            return TOP
        if move[0] == 0 and (move[1] == 0 or name == self.root + "_gen"):
            return (0, move[1] + 1)
        if move[0] <= -4:
            return (move[0] + 4, move[1])
        return TOP

    def call_value(self, toks, states, env):
        """A call whose value is kept: {(state after it, the move its value
        holds, or None)}."""
        close = matching(toks, 1)
        args = split_top(toks[2:close], ",") if close > 2 else []
        for a in args:
            states = self.expr(a, states, env)
        return self.call(toks[0], args, states, env, value=True)

    def call(self, tok, args, states, env, value=False):
        """{(state after the call, the move its value holds, or None)}."""
        name = env.get(tok.text, tok.text)
        if GUEST_FN.fullmatch(name):
            return {((self.guest_ret(name, m), kept), None) for m, kept in states}
        defs = self.code.lookup(name, self.src)
        if not defs or not (value or self.code.moves(defs)):
            return {(s, None) for s in states}
        if len(self.stack) >= MAX_DEPTH or name in self.stack:
            return {((TOP, kept), None) for _, kept in states}
        out = set()
        for f in defs:
            bind = {}
            for p, a in zip(f.params, args):
                a = strip_parens(a)
                if p and len(a) == 1 and a[0].kind == "id" and self.fn(a[0].text, env):
                    bind[p] = self.fn(a[0].text, env)
            for state in states:
                # The helper starts with its parameters that hold g_esp kept,
                # and none of the caller's locals; the caller's are as they
                # were when it returns.
                held = tuple(sorted((p, v) for p, a in zip(f.params, args) if p
                                    for v in [self.form(a, state)] if v is not None))
                key = (id(f), tuple(sorted(bind.items())), state[0], held)
                if key not in self.cache:
                    self.stack.append(name)
                    try:
                        self.cache[key] = {(m, v) for (m, _), (_, _, v) in self.run(f, {(state[0], held)}, bind)}
                    finally:
                        self.stack.pop()
                out |= {((m, state[1]), v) for m, v in self.cache[key]}
        if len(out) > MAX_STATES:
            out = {((TOP, kept), None) for (_, kept), _ in out}
        return out if len(out) <= MAX_STATES else {((TOP, ()), None)}

    # -- statements --------------------------------------------------------

    def run(self, func, states, env):
        """A function's way out of each path: [(state, (line, "return" or
        "end", the move its value holds or None))]."""
        saved = self.exits, self.src
        self.exits, self.src = [], func.source
        try:
            out, brk, cont = self.stmt(func.stmts(), set(states), dict(env))
            if brk or cont:
                raise Unreadable(func.line, "a break or continue outside a loop")
            return self.exits + [(s, (func.end_line, "end", None)) for s in out]
        finally:
            self.exits, self.src = saved

    def stmt(self, node, states, env):
        """(states after it, states at its breaks, states at its continues)."""
        none = set()
        if node is None or not states:
            return states, none, none
        kind = node[0]
        if kind == "block":
            brk, cont = set(), set()
            for item in node[1]:
                if item[0] == "label":
                    raise Unreadable(item[2], f"a {item[1]} label outside its switch's own block")
                states, b, c = self.stmt(item, states, env)
                brk |= b
                cont |= c
            return states, brk, cont
        if kind == "expr":
            return self.expr(node[1], states, env), none, none
        if kind == "return":
            value = strip_parens(node[1])
            if value and self.whole_call(value):
                pairs = self.call_value(value, states, env)
            else:
                pairs = {(s, self.value(value, s)) for s in self.expr(node[1], states, env)}
            self.exits.extend((s, (node[2], "return", v)) for s, v in pairs)
            return set(), none, none
        if kind == "break":
            return set(), set(states), none
        if kind == "continue":
            return set(), none, set(states)
        if kind == "if":
            true, false = self.truth(node[1], states, env)
            a, ab, ac = self.stmt(node[2], true, env)
            b, bb, bc = self.stmt(node[3], false, env) if node[3] else (false, none, none)
            return self.norm(a | b), ab | bb, ac | bc
        if kind == "ppif":
            out, brk, cont = set(), set(), set()
            for branch in node[1]:
                a, b, c = self.stmt(branch, states, dict(env))
                out, brk, cont = out | a, brk | b, cont | c
            return self.norm(out), brk, cont
        if kind == "loop":
            return self.loop(node, states, env), none, none
        if kind == "switch":
            states = self.expr(node[1], states, env)
            items = node[2][1] if node[2] and node[2][0] == "block" else [node[2]]
            cur, brk, cont, default = set(), set(), set(), False
            for item in items:
                if item[0] == "label":
                    default |= item[1] == "default"
                    cur |= states
                    continue
                cur, b, c = self.stmt(item, cur, env)
                brk |= b
                cont |= c
            return self.norm(cur | brk | (set() if default else states)), none, cont
        raise Unreadable(0, f"a {kind} statement")

    def loop(self, node, states, env):
        """The states a loop leaves with: where its condition is false, and at
        its breaks. Its body runs again until no new state comes round."""
        _, how, init, cond, step, body, line = node
        value = self.const(cond) if cond else 1
        work, seen, out = self.expr(init, states, env), set(), set()
        rets = max((m[1] for m, _ in work if m != TOP), default=0)
        for n in range(MAX_LOOP):
            new = work - seen
            if not new:
                return self.norm(out)
            seen |= new
            if how != "do":
                new, false = self.truth(cond, new, env)
                if not value:           # a condition that can be false
                    out |= false
            after, brk, cont = self.stmt(body, new, env)
            out |= brk
            nxt = self.expr(step, after | cont, env)
            if how == "do":
                nxt, false = self.truth(cond, nxt, env)
                if not value:
                    out |= false
                if value == 0:          # do { } while (0)
                    return self.norm(out)
            # A `ret` in the body that can come round again could be done
            # more than once, or not at all where the pass that does it is
            # picked by a count: that isn't followed. One a break leaves with
            # is.
            if any(m != TOP and m[1] > rets for m, _ in nxt):
                raise Unreadable(line, "a `ret` inside a loop that can come round again")
            # A move that keeps changing round the loop (a push for each of n
            # arguments) is given up on, and then what locals hold, so it
            # comes round the same.
            if n >= WIDEN:
                nxt = {(TOP, kept if n < 2 * WIDEN else ()) for _, kept in nxt}
            work = self.norm(nxt)
        return {(TOP, ())}


# ---------------------------------------------------------------------------
# The code read, and its overrides

class Source:
    """One C file, read as the recompiler counts it."""

    def __init__(self, path):
        self.path = pathlib.Path(path)
        text = self.path.read_text(encoding="utf-8", errors="replace")
        toks = tokenize(blank("\n".join(live_lines(text))))
        fmacros, self.consts = pp_defines(toks)
        self.funcs = functions(toks, fmacros)
        for fs in self.funcs.values():
            for f in fs:
                f.source = self

    def overrides(self):
        """The game's functions this file defines for the recompiler to leave
        out: those manual_scan finds it defines or wraps, and any a macro of
        the file defines. {name: [Func]}, with no Func where none was found."""
        skip, wrap, _ = scan(str(self.path))
        own = {int(OVERRIDE.fullmatch(n).group(1), 16): n for n in self.funcs if OVERRIDE.fullmatch(n)}
        vas = skip | wrap | set(own)
        return {own.get(va, f"sub_{va:08X}"): self.funcs.get(own.get(va), []) for va in sorted(vas)}


class Code:
    """The files read together: a call is found in its own file first, then
    among the others' functions that aren't static."""

    def __init__(self, paths):
        self.sources = [Source(p) for p in paths]
        self.public = {}
        for src in self.sources:
            for name, fs in src.funcs.items():
                self.public.setdefault(name, []).extend(f for f in fs if not f.static)
        self._movers = None

    def lookup(self, name, src):
        if src is not None and name in src.funcs:
            return src.funcs[name]
        return self.public.get(name, [])

    def moves(self, defs):
        """Whether any of defs can move g_esp: it writes it, names one of the
        game's functions, calls one of its parameters, or names a function
        that can. Those that can't are passed over."""
        if self._movers is None:
            self._movers = self.find_movers()
        return any(id(f) in self._movers for f in defs)

    def find_movers(self):
        uses, movers = {}, set()
        for src in self.sources:
            for fs in src.funcs.values():
                for f in fs:
                    texts = [t.text for t in f.body]
                    named = set()
                    for k, s in enumerate(texts):
                        nxt = texts[k + 1] if k + 1 < len(texts) else None
                        prev = texts[k - 1] if k else None
                        if s == SP and (nxt in ASSIGN_OPS or prev == "&" or "++" in (prev, nxt)
                                        or "--" in (prev, nxt)):
                            movers.add(id(f))
                        elif GUEST_FN.fullmatch(s) or (s in f.params and nxt == "("):
                            movers.add(id(f))
                        elif f.body[k].kind == "id":
                            named.add(s)
                    uses[id(f)] = [g for s in named for g in self.lookup(s, src)]
        grown = True
        while grown:
            grown = False
            for fid, gs in uses.items():
                if fid not in movers and any(id(g) in movers for g in gs):
                    movers.add(fid)
                    grown = True
        return movers


def judge(exits):
    """(how the paths do their ret, [(line, what's wrong)]) for one
    override's exits."""
    hows, wrong, by_hand = set(), set(), {}
    for (move, _), (line, kind, _) in exits:
        where = f"the return at line {line}" if kind == "return" else f"the path to its end at line {line}"
        if move == TOP:
            wrong.add((line, f"{where}: where {SP} is can't be followed"))
        elif move == (0, 1):
            hows.add("the game's body, or a call of the game's")
        elif move[1] == 0 and move[0] >= 4 and move[0] % 4 == 0:
            hows.add(f"by hand, +{move[0]}")
            by_hand.setdefault(move[0], line)
        elif move == (0, 0):
            wrong.add((line, f"{where} leaves the return address on the guest stack: no `ret`, by the "
                             "game's body, a call of the game's, or by hand"))
        elif move[1] >= 2:
            wrong.add((line, f"{where} does the `ret` {move[1]} times"))
        elif move[1] == 1:
            wrong.add((line, f"{where} moves {SP} {move[0]:+d} after the `ret` the game's code did"))
        else:
            wrong.add((line, f"{where} leaves {SP} {move[0]:+d} from where it came in, not a `ret`"))
    if len(by_hand) > 1:
        pops = ", ".join(f"+{k} at line {v}" for k, v in sorted(by_hand.items()))
        wrong.add((min(by_hand.values()), f"its paths pop different amounts by hand ({pops})"))
    return sorted(hows), sorted(wrong)


def check(paths):
    """{path: [(override, line, how its paths do their ret, [(line, what's
    wrong)])]}, for the overrides each file defines, the calls followed
    through all of them."""
    code, out = Code(paths), {}
    for src in code.sources:
        rows = out.setdefault(str(src.path), [])
        for name, defs in src.overrides().items():
            if not defs:
                rows.append((name, 0, [], [(0, "manual_scan finds it, but not its definition")]))
            for f in defs:
                try:
                    hows, wrong = judge(Walk(code, name).run(f, {((0, 0), ())}, {}))
                except Unreadable as e:
                    hows, wrong = [], [(e.line or f.line, f"can't be followed: {e.why}")]
                rows.append((name, f.line, hows, wrong))
    return out


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("files", nargs="*", help="the C files to read (every port/*.c without any)")
    ap.add_argument("--list", action="store_true", help="also print how each override's paths do their ret")
    args = ap.parse_args(argv)
    paths = [pathlib.Path(f) for f in args.files] or sorted((REPO / "port").glob("*.c"))
    missing = [str(p) for p in paths if not p.is_file()]
    if missing or not paths:
        print(f"override_ret: no such file: {', '.join(missing) or 'port/*.c'}", file=sys.stderr)
        return 2
    count = listed = 0
    for path, rows in check(paths).items():
        try:
            shown = pathlib.Path(path).resolve().relative_to(REPO)
        except ValueError:
            shown = path
        for name, line, hows, wrong in rows:
            count += 1
            if args.list:
                print(f"{shown}:{line}: {name}: {'; '.join(hows) or '-'}")
            for wline, what in wrong:
                listed += 1
                print(f"{shown}:{wline}: {name}: {what}")
    print(f"override_ret: {count} overrides read, " +
          (f"{listed} paths listed" if listed else "every path does its `ret`"))
    return 1 if listed else 0


if __name__ == "__main__":
    sys.exit(main())
