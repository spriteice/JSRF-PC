"""Whether two builds of the engine differ only as a move makes them.

    python3 tools/compare_engines.py OLD NEW [--extern NAME ...] [--objdump PROG]
                                     [--out DIR]

OLD and NEW are two build folders of the engine, from the base tree and
the change's tree, built the same way with each checkout at the same path
(docs/build-and-test-reference.md, The same engine). Every object and
static library under each is read: *.o, *.obj and *.a, by its path in the
folder and, in a library, its member's name. x86-64 objects only, Linux's
(ELF) and Windows' (COFF).

First the objects: those with the same SHA-256 are the same, and if every
one is, the engines are the same and nothing else is compared. Then the
symbols of the objects that differ, as nm -S lists them: each whose size,
type (t to T: a static gone extern) or object changed, and those in one
engine only. Windows' objects carry no sizes, so there a function's size
runs to the next symbol. Then every function, by name, wherever it now is,
disassembled by objdump with the addresses left out: each reference is
named by what it points at (a function or variable by its name, a string
by its text, a constant by its bytes, and a field in it that is itself
relocated, such as a switch table's entry, by what that points at) and a
jump inside the function by how many instructions away it lands, so that
the order of a file's strings and statics, which a move changes, doesn't
show. The nops that align code are left out, and a function's address
loaded from the GOT reads as the lea that takes it in its own file.

Each function is sorted: the same; the same but moved to another object
or gone extern (and so, with -fcf-protection, starting with endbr64);
different where it calls, on one side only, a function the move changed
(moved, gone extern, only in one engine, or named with --extern) or one
the move put a file between it and (in its file on one side only, so
inlined there), with what those call in turn, and those callees named;
only in one engine, as a static or a compiler's copy (.constprop, .isra,
.part, .cold) inlined on the other side, or as a function named with
--extern, a static the move made extern (move_check.py lists them) that
was inlined before; and anything else.
Data is compared by its symbols only (move_check.py checks the text that
gives it its values). The sorting names a cause; it doesn't prove one.

The exit status is 0 when nothing differs or everything that differs is
of a move's kinds, 1 when anything else does (listed first), and 2 when
it can't run. With --out, each function's text and each diff are written
to that folder too. Standard library only, with binutils' objdump, which
reads both kinds of object.
"""
import argparse
import bisect
import collections
import difflib
import hashlib
import os
import pathlib
import re
import shutil
import struct
import subprocess
import sys
import tempfile

PATTERNS = ("*.o", "*.obj", "*.a")
CLONE = re.compile(r"^(.+?)\.(?:constprop|isra|part|cold|lto_priv|clone)(?:\.\d+)*")
FUNC_HEAD = re.compile(r"^([0-9a-f]+) <(.+)>:$")
INSN = re.compile(r"^\s*([0-9a-f]+):\t(.*)$")
RELOC = re.compile(r"^\s*([0-9a-f]+): (R_X86_64_\S+|IMAGE_REL_AMD64_\S+)\s+(.*)$")
SECTION = re.compile(r"^Disassembly of section (.+):$")
DIRECT = re.compile(r"^([0-9a-f]+) <(.+)>$")
RIP = re.compile(r"(-?0x[0-9a-f]+)?\(%rip\)")
HEX = re.compile(r"(\$?)(-?0x[0-9a-f]+)\b")
TARGET = re.compile(r"^(.*?)(?:([+-])0x([0-9a-f]+))?$")
LOCAL_STATIC = re.compile(r"^([A-Za-z_]\w*)\.\d+$")
COMMENT = re.compile(r"^\s*([0-9a-f]+) <(.+)>")
CALL = re.compile(r"^(?:call|jmp)\w*\s+([^\s*%.$][^\s]*)$")
PADDING = re.compile(r"^(?:(?:data16|cs|ds|addr32)\s+)*(?:nop[wl]?|xchg\s+%ax,%ax|int3)\b")
PREFIX = re.compile(r"^(?:rep\w*|lock|notrack|bnd|data16|cs|ds|addr32)$")
ELF_PC_RELATIVE = ("PC32", "PLT32", "GOTPCREL", "GOTPCRELX", "REX_GOTPCRELX", "GOTTPOFF",
                   "TLSGD", "TLSLD", "PC64", "GOTPC32")
ELF_SUFFIX = {"GOTPCREL": "@GOT", "GOTPCRELX": "@GOT", "REX_GOTPCRELX": "@GOT",
              "GOTTPOFF": "@tpoff", "TPOFF32": "@tpoff", "TLSGD": "@tls", "TLSLD": "@tls",
              "DTPOFF32": "@tls"}
ELF_WIDTH = {1: 8, 24: 8, 12: 2, 13: 2, 14: 1, 15: 1}       # R_X86_64_64, PC64, 16, 8
ELF_ABSOLUTE = {1, 10, 11, 12, 14}                           # 64, 32, 32S, 16, 8
COFF_WIDTH = {1: 8, 10: 2}                                   # ADDR64, SECTION
COFF_ABSOLUTE = {1, 2, 3}                                    # ADDR64, ADDR32, ADDR32NB
NOP = re.compile(r"^(?:(?:data16|cs|ds|addr32)\s+)*(?:nop[wlq]?|xchg\s+%ax,%ax)(?:\s|$)")
GOT_LOAD = re.compile(r"^mov[q]? <([^<>]+)@GOT>\(%rip\),(%r\w+)$")
KINDS = ("the same", "moved or gone extern", "a call where a body was inlined",
         "only in one engine", "anything else")
MOVE_KINDS = KINDS[:4]


class CompareError(Exception):
    pass


# ---------------------------------------------------------------- objects

class Symbol:
    __slots__ = ("name", "section", "value", "size", "glob", "kind", "label")

    def __init__(self, name, section, value, size, glob, kind, label=False):
        self.name, self.section, self.value, self.size = name, section, value, size
        self.glob, self.kind, self.label = glob, kind, label


class Section:
    __slots__ = ("name", "data", "size", "code", "nobits", "write", "strings", "entsize")

    def __init__(self, name, data, size, code, nobits, write, strings=False, entsize=0):
        self.name, self.data, self.size, self.code = name, data, size, code
        self.nobits, self.write, self.strings, self.entsize = nobits, write, strings, entsize

    def letter(self):
        return "t" if self.code else "b" if self.nobits else "d" if self.write else "r"


class Object:
    """An object's sections and defined symbols, read from its bytes."""

    def __init__(self, key, data):
        self.key, self.data = key, data
        self.sections, self.symbols = [], []
        self.coff = data[:4] != b"\x7fELF"
        if self.coff and data[:2] != b"\x64\x86" and data[:4] != b"\x00\x00\xff\xff":
            raise CompareError("%s isn't an x86-64 ELF or COFF object" % key)
        self.relocs = {}              # data section -> {offset: (width, target)}
        self.by_index = {}            # symbol index -> (name, section or None, value, label)
        if self.coff:
            self._coff()
        else:
            self._elf()
        self.by_name = collections.defaultdict(list)
        self.places = collections.defaultdict(list)    # section -> [(value, end, name)]
        self.first_named = {}
        for s in self.symbols:
            self.by_name[s.name].append(s)
            if not s.label and s.kind != "section":
                self.places[s.section].append((s.value, s.value + max(s.size, 1), s.name))
        for places in self.places.values():
            places.sort()
        self.reloc_at = {k: sorted(v) for k, v in self.relocs.items()}
        self.starts = {k: [v[0] for v in places] for k, places in self.places.items()}
        for i, s in enumerate(self.sections):
            self.first_named.setdefault(s.name, i)

    def _elf(self):
        d = self.data
        if d[4] != 2 or d[5] != 1 or struct.unpack_from("<H", d, 18)[0] != 62:
            raise CompareError("%s isn't a 64-bit x86-64 ELF object" % self.key)
        shoff, = struct.unpack_from("<Q", d, 0x28)
        shentsize, shnum, shstrndx = struct.unpack_from("<HHH", d, 0x3A)
        heads = [struct.unpack_from("<IIQQQQIIQQ", d, shoff + i * shentsize)
                 for i in range(shnum)]
        if shnum == 0 and shoff:
            first = struct.unpack_from("<IIQQQQIIQQ", d, shoff)
            heads = [struct.unpack_from("<IIQQQQIIQQ", d, shoff + i * shentsize)
                     for i in range(first[5])]
        if shstrndx == 0xFFFF:
            shstrndx = heads[0][6]
        names = heads[shstrndx]

        def text(table, at):
            start = table[4] + at
            return d[start:d.index(b"\0", start)].decode("utf-8", "replace")
        for name, typ, flags, _a, off, size, _l, _i, _al, entsize in heads:
            nobits = typ == 8
            self.sections.append(Section(
                text(names, name), b"" if nobits else d[off:off + size], size,
                bool(flags & 0x4), nobits, bool(flags & 0x1), bool(flags & 0x20), entsize))
        for head in heads:
            if head[1] != 2:          # SHT_SYMTAB
                continue
            strtab = heads[head[6]]
            for n, at in enumerate(range(head[4], head[4] + head[5], 24)):
                st_name, info, _o, shndx, value, size = struct.unpack_from("<IBBHQQ", d, at)
                kind = info & 0xF
                defined = 0 < shndx < 0xFF00
                if kind == 3 and defined:
                    self.by_index[n] = (self.sections[shndx].name, shndx, 0, True)
                elif n:
                    name = text(strtab, st_name)
                    self.by_index[n] = (name, shndx if defined else None, value,
                                        not name or name.startswith(".L"))
                if not defined or kind == 4:
                    continue          # undefined, absolute or common, a file's name
                section = self.sections[shndx]
                if kind == 3:         # STT_SECTION
                    self.symbols.append(Symbol(section.name, shndx, 0, 0, False, "section",
                                               label=True))
                    continue
                name = text(strtab, st_name)
                label = not name or name.startswith(".L")   # an assembler's own name
                local = LOCAL_STATIC.match(name)
                if local and info >> 4 == 0 and not section.code:
                    name = local.group(1)     # a function's own static: on.86 is on
                self.symbols.append(Symbol(name, shndx, value, size, info >> 4 in (1, 2),
                                           "func" if section.code else "data", label))
        for head in heads:
            if head[1] != 4 or self.sections[head[7]].code:
                continue              # SHT_RELA, for data: code's are objdump's
            fields = self.relocs.setdefault(head[7], {})
            for at in range(head[4], head[4] + head[5], 24):
                offset, info, addend = struct.unpack_from("<QQq", d, at)
                rtype = info & 0xFFFFFFFF
                fields[offset] = (ELF_WIDTH.get(rtype, 4),
                                  (self.by_index.get(info >> 32), addend,
                                   rtype in ELF_ABSOLUTE))

    def _coff(self):
        d = self.data
        if d[:4] == b"\x00\x00\xff\xff":
            machine, = struct.unpack_from("<H", d, 6)
            nsec, symtab, nsym = struct.unpack_from("<III", d, 44)
            head_size, sym_size, sec_fmt = 56, 20, "<8sIiHBB"
        else:
            machine, nsec = struct.unpack_from("<HH", d, 0)
            symtab, nsym = struct.unpack_from("<II", d, 8)
            optional, = struct.unpack_from("<H", d, 16)
            head_size, sym_size, sec_fmt = 20 + optional, 18, "<8sIhHBB"
        if machine != 0x8664:
            raise CompareError("%s isn't an x86-64 COFF object" % self.key)
        strings = symtab + nsym * sym_size

        def name_of(raw):
            if raw[:4] == b"\0\0\0\0":
                at = strings + struct.unpack_from("<I", raw, 4)[0]
                return d[at:d.index(b"\0", at)].decode("utf-8", "replace")
            return raw.rstrip(b"\0").decode("utf-8", "replace")
        reloc_tables = []
        for i in range(nsec):
            raw, _vs, _va, size, ptr, rptr, _pl, nrel, _nl, chars = struct.unpack_from(
                "<8sIIIIIIHHI", d, head_size + i * 40)
            if chars & 0x01000000 and nrel == 0xFFFF:     # more than 0xFFFF: the
                nrel = struct.unpack_from("<I", d, rptr)[0]  # first holds the count
                rptr, nrel = rptr + 10, nrel - 1
            reloc_tables.append((rptr, nrel))
            name = name_of(raw)
            if name.startswith("/") and name[1:].isdigit():
                at = strings + int(name[1:])
                name = d[at:d.index(b"\0", at)].decode("utf-8", "replace")
            nobits = bool(chars & 0x80)
            self.sections.append(Section(
                name, b"" if nobits else d[ptr:ptr + size], size,
                bool(chars & 0x20 or chars & 0x20000000), nobits, bool(chars & 0x80000000)))
        i = 0
        while i < nsym:
            raw, value, secno, typ, klass, aux = struct.unpack_from(
                sec_fmt, d, symtab + i * sym_size)
            name = name_of(raw)
            own = secno > 0 and name == self.sections[secno - 1].name and value == 0
            self.by_index[i] = (name, secno - 1 if secno > 0 else None, value,
                                bool(own and klass == 3 and aux) or name.startswith(".L"))
            i += 1 + aux
            if secno <= 0 or klass not in (2, 3):
                continue              # undefined, absolute, debug; only extern and static
            section = self.sections[secno - 1]
            if klass == 3 and value == 0 and aux and name == section.name:
                self.symbols.append(Symbol(name, secno - 1, 0, 0, False, "section", True))
                continue
            self.symbols.append(Symbol(name, secno - 1, value, None, klass == 2,
                                       "func" if section.code else "data",
                                       name.startswith(".L")))
        # A relocated field in data holds its addend; code's are objdump's.
        for n, (rptr, nrel) in enumerate(reloc_tables):
            sec = self.sections[n]
            if sec.code or sec.nobits or not nrel:
                continue
            fields = self.relocs.setdefault(n, {})
            for at in range(rptr, rptr + nrel * 10, 10):
                offset, index, rtype = struct.unpack_from("<IIH", d, at)
                width = COFF_WIDTH.get(rtype, 4)
                addend = int.from_bytes(sec.data[offset:offset + width], "little", signed=True)
                fields[offset] = (width, (self.by_index.get(index), addend,
                                          rtype in COFF_ABSOLUTE))
        # COFF has no sizes: each symbol runs to the next one in its section.
        starts = collections.defaultdict(list)
        for s in self.symbols:
            if not s.label:
                starts[s.section].append(s.value)
        for s in self.symbols:
            if s.size is None:
                later = [v for v in starts[s.section] if v > s.value]
                s.size = (min(later) if later else self.sections[s.section].size) - s.value

    def defined(self):
        """The named symbols nm would list, by name."""
        return [s for s in self.symbols if not s.label and s.kind != "section"]

    def named_over(self, section, offset):
        """The named symbol over that place in the section, if one is."""
        places = self.places.get(section, ())
        i = bisect.bisect_right(self.starts.get(section, ()), offset) - 1
        for value, end, name in reversed(places[max(0, i - 3):i + 1]):
            if value <= offset < end:
                return name, offset - value
        return None

    def resolve(self, name, offset):
        """What a reference to name+offset points at, without addresses: a
        named symbol by its name, an anonymous place by its contents."""
        local = LOCAL_STATIC.match(name)
        if local and name not in self.by_name and local.group(1) in self.by_name:
            name = local.group(1)
        syms = [s for s in self.by_name.get(name, ()) if s.kind != "section"]
        section = None
        if syms and not syms[0].label:
            return name + ("%+#x" % offset if offset else "")
        if syms:
            section, offset = syms[0].section, syms[0].value + offset
        else:
            own = [s.section for s in self.by_name.get(name, ()) if s.kind == "section"]
            section = own[0] if len(own) == 1 else self.first_named.get(name)
        if section is None:           # not defined here: an extern
            return name + ("%+#x" % offset if offset else "")
        sec = self.sections[section]
        over = self.named_over(section, offset)
        # COFF gives no sizes, so a static in read-only data seems to run on
        # over the strings after it: past its start, name the place by its
        # contents instead.
        if over and over[1] and self.coff and not (sec.code or sec.write):
            over = None
        if over:
            return over[0] + ("%+#x" % over[1] if over[1] else "")
        if sec.code:
            return "%s%+#x" % (sec.name, offset)
        return self._contents(section, offset)

    def _contents(self, section, offset, depth=0):
        """An anonymous place in data, by its contents: a string by its
        text, anything else by its bytes, with each relocated field in
        them shown by what it points at, as its bytes move with the code."""
        sec = self.sections[section]
        if sec.nobits:
            return "<%s, no contents>" % sec.name
        data = sec.data[offset:]
        end = data.find(b"\0")
        text = _text(data[:end]) if end > 0 else None
        if text is not None and not self._relocated(section, offset, end + 1):
            return '"%s"' % text.encode("unicode_escape").decode("ascii")
        if (end == 0 and (sec.strings or (self.coff and not sec.write))
                and not self._relocated(section, offset, 1)):
            # COFF doesn't mark a section of strings: a NUL in read-only
            # data, where a reference lands, is taken as an empty string.
            return '""'
        size = sec.entsize if 0 < sec.entsize <= 32 else 8
        return "<%s %s>" % (sec.name, self._bytes(section, offset, size, depth))

    def _relocated(self, section, offset, length):
        """Whether a relocated field lies in those bytes."""
        starts = self.reloc_at.get(section, ())
        for at in starts[bisect.bisect_left(starts, offset - 8):]:
            if at >= offset + length:
                break
            if at + self.relocs[section][at][0] > offset:
                return True
        return False

    def _bytes(self, section, offset, size, depth):
        data, fields = self.sections[section].data, self.relocs.get(section, {})
        starts = self.reloc_at.get(section, ())
        shown, at = [], offset
        for start in starts[bisect.bisect_left(starts, offset - 8):]:
            width, target = fields[start]
            if start >= offset + size:
                break
            if start + width <= offset:
                continue
            if start > at:
                shown.append(data[at:start].hex())
            shown.append("[%s]" % (self._target(target, depth) if depth < 2 else "..."))
            at = start + width
        if at < offset + size:
            shown.append(data[at:offset + size].hex())
        return "".join(shown)

    def _target(self, target, depth):
        """What a relocated field in data points at: a symbol by its name;
        a place in code by the function it's in; a place in data by its
        contents; and, for a field relative to where it is (a switch
        table's entry), only the section, as its addend holds its place."""
        sym, addend, absolute = target
        if sym is None:
            return "?"
        name, section, value, label = sym
        if section is None or not label:
            local = LOCAL_STATIC.match(name)
            if local and section is not None and not self.sections[section].code:
                name = local.group(1)
            return name + ("%+#x" % addend if absolute and addend else "")
        sec = self.sections[section]
        if not absolute:
            return sec.name
        offset = value + addend
        if sec.code:
            over = self.named_over(section, offset)
            return over[0] if over else sec.name
        over = self.named_over(section, offset)
        if over and not (over[1] and self.coff and not sec.write):
            return over[0] + ("%+#x" % over[1] if over[1] else "")
        return self._contents(section, offset, depth + 1)


def _text(raw):
    """The bytes as text, if they are UTF-8 that prints."""
    try:
        text = raw.decode("utf-8")
    except UnicodeDecodeError:
        return None
    return text if all(c.isprintable() or c in "\t\n\r" for c in text) else None


def read_folder(folder):
    """{key: bytes} for every object under the folder, a library's members
    as "lib.a(member)"."""
    root, found = pathlib.Path(folder), {}
    for pattern in PATTERNS:
        for path in sorted(root.rglob(pattern)):
            if not path.is_file():
                continue
            rel = path.relative_to(root).as_posix()
            data = path.read_bytes()
            if pattern == "*.a":
                found.update(members(rel, data))
            else:
                found[rel] = data
    return found


def members(rel, data):
    """A GNU, COFF or BSD ar library's members, by name."""
    if data[:8] != b"!<arch>\n":
        raise CompareError("%s isn't an ar library (a thin one can't be read)" % rel)
    out, at, longnames, seen = {}, 8, b"", collections.Counter()
    while at + 60 <= len(data):
        head = data[at:at + 60]
        name = head[:16].decode("ascii", "replace").rstrip()
        size = int(head[48:58].decode("ascii").strip())
        body = data[at + 60:at + 60 + size]
        at += 60 + size + (size & 1)
        if name in ("/", "/SYM64/", "__.SYMDEF", "__.SYMDEF SORTED"):
            continue
        if name == "//":
            longnames = body
            continue
        if name.startswith("#1/"):    # BSD: the name leads the body
            n = int(name[3:])
            name, body = body[:n].rstrip(b"\0").decode("utf-8", "replace"), body[n:]
        elif name.startswith("/") and name[1:].isdigit():
            start = int(name[1:])     # GNU ends a long name with "/\n", COFF's with a NUL
            end = min(i for i in (longnames.find(b"\n", start), longnames.find(b"\0", start),
                                   len(longnames)) if i >= 0)
            name = longnames[start:end].decode("utf-8", "replace")
        name = name.rstrip("/")
        seen[name] += 1
        key = "%s(%s)" % (rel, name if seen[name] == 1 else "%s#%d" % (name, seen[name]))
        out[key] = body
    return out


# ---------------------------------------------------------------- functions

def disassemble(obj, objdump, tmp):
    """{function name: [instruction text]} for the object, addresses left out."""
    path = os.path.join(tmp, "object")
    with open(path, "wb") as f:
        f.write(obj.data)
    p = subprocess.run([objdump, "-d", "-r", "--no-show-raw-insn", path],
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if p.returncode != 0:
        raise CompareError("objdump couldn't read %s: %s"
                           % (obj.key, p.stderr.decode("utf-8", "replace").strip()))
    coff = obj.data[:4] != b"\x7fELF"
    functions, current = {}, None
    for line in p.stdout.decode("utf-8", "replace").split("\n"):
        m = FUNC_HEAD.match(line)
        if m:
            current = (m.group(2), int(m.group(1), 16), [])
            functions.setdefault(m.group(2), []).append(current)
            continue
        if SECTION.match(line):
            current = None
            continue
        if current is None:
            continue
        m = RELOC.match(line)
        if m and current[2]:
            current[2][-1][2].append((int(m.group(1), 16), m.group(2), m.group(3).strip()))
            continue
        m = INSN.match(line)
        if m:
            current[2].append([int(m.group(1), 16), m.group(2), []])
    out = {}
    for name, copies in functions.items():
        for n, (_name, _start, insns) in enumerate(copies):
            key = name if n == 0 else "%s#%d" % (name, n + 1)
            out[key] = normalise(obj, insns, coff, _start)
    return out


def normalise(obj, insns, coff, start):
    """The function's instructions as text with no address in it. Its
    nops are left out, as their lengths follow the function's alignment,
    and a jump counts the instructions to where it lands without them."""
    nops = [bool(NOP.match(" ".join(text.partition("#")[0].split())))
            for _a, text, _r in insns]
    index, kept = {}, sum(1 for nop in nops if not nop)
    for i in range(len(insns) - 1, -1, -1):
        if not nops[i]:
            kept -= 1
        index[insns[i][0]] = kept    # a jump onto a nop lands on what follows it
    lines = []
    for i, (addr, text, relocs) in enumerate(insns):
        if nops[i]:
            continue
        nxt = insns[i + 1][0] if i + 1 < len(insns) else None
        here = index[addr]
        text, _hash, comment = text.partition("#")
        parts = text.split(None, 1)
        if not parts:
            continue
        op = parts[0]
        rest = parts[1].strip() if len(parts) > 1 else ""
        # Prefixes (rep, lock, notrack, data16, cs) stay with the mnemonic.
        while PREFIX.match(op.split()[-1]) and rest:
            more = rest.split(None, 1)
            op += " " + more[0]
            rest = more[1] if len(more) > 1 else ""
        direct = DIRECT.match(rest)
        for at, rtype, target in relocs:
            resolved = _resolve_reloc(obj, rtype, target, at, nxt, rest, direct, coff)
            rest = _substitute(rest, resolved, rtype, at, nxt, direct)
            direct = None
        if direct:
            to = int(direct.group(1), 16)
            if to in index:
                rest = ".%+d" % (index[to] - here)
            else:
                rest = re.sub(r"\+0x0$", "", direct.group(2))
        rip = RIP.search(rest) if not relocs else None
        seen = COMMENT.match(comment) if rip else None
        if seen:                      # a place in the same section, with no relocation
            to = int(seen.group(1), 16)
            there = ".%+d" % (index[to] - here) if to in index else \
                re.sub(r"\+0x0$", "", seen.group(2))
            rest = rest[:rip.start()] + "<%s>(%%rip)" % there + rest[rip.end():]
        line = (op + " " + " ".join(rest.split())).strip()
        # Loading a function's address from the GOT, or taking it with lea,
        # is the same address: which one the compiler uses follows whether
        # the function is in the same file.
        lines.append(GOT_LOAD.sub(r"lea <\1>(%rip),\2", line))
    while lines and PADDING.match(lines[-1]):
        lines.pop()
    return lines


def _resolve_reloc(obj, rtype, target, at, nxt, rest, direct, coff):
    """What a relocation in an instruction points at: ELF's addend is in
    the relocation, COFF's in the field the operand prints."""
    m = TARGET.match(target)
    name, sign, mag = m.group(1), m.group(2), m.group(3)
    addend = (int(mag, 16) * (-1 if sign == "-" else 1)) if mag else 0
    short = rtype.split("_", 3)[-1] if not coff else rtype[len("IMAGE_REL_AMD64_"):]
    if coff:
        if short.startswith("REL32"):
            if direct:
                offset = int(direct.group(1), 16) - (nxt if nxt is not None else at + 4)
            else:
                rip = RIP.search(rest)
                offset = int(rip.group(1), 16) if rip and rip.group(1) else 0
        else:
            offset = _printed_field(rest, at, nxt, short == "ADDR64")
        return obj.resolve(name, offset)
    if short in ELF_PC_RELATIVE:
        offset = addend + ((nxt if nxt is not None else at + 4) - at)
    else:
        offset = addend
    return obj.resolve(name, offset) + ELF_SUFFIX.get(short, "")


def _printed_field(rest, at, nxt, wide):
    """The value an absolute relocation's field holds, as the operand
    prints it: COFF keeps the addend there."""
    token = _field_token(rest, at, nxt, wide)
    return int(token.group(2), 16) if token else 0


def _field_token(rest, at, nxt, wide):
    tokens = [t for t in HEX.finditer(rest) if not rest[t.end():].startswith("(%rip)")]
    imms = [t for t in tokens if t.group(1)]
    disps = [t for t in tokens if not t.group(1)]
    last = nxt is not None and at + (8 if wide else 4) == nxt
    if last and imms:
        return imms[-1]
    if disps:
        return disps[0]
    return imms[-1] if imms else None


def _substitute(rest, resolved, rtype, at, nxt, direct):
    if direct:
        return resolved
    rip = RIP.search(rest)
    if rip and ("PC" in rtype or "PLT" in rtype or "GOT" in rtype or "REL32" in rtype
                or "TLSGD" in rtype or "TLSLD" in rtype):
        return rest[:rip.start()] + "<%s>(%%rip)" % resolved + rest[rip.end():]
    token = _field_token(rest, at, nxt, rtype.endswith("_64") or rtype.endswith("ADDR64"))
    if token:
        return rest[:token.start()] + "%s<%s>" % (token.group(1), resolved) + rest[token.end():]
    return rest + " <%s>" % resolved


# ---------------------------------------------------------------- comparing

def base_name(name):
    m = CLONE.match(name)
    return m.group(1) if m else name


def callees(lines):
    found = set()
    for line in lines:
        m = CALL.match(line)
        if m and not m.group(1).startswith("<"):
            found.add(base_name(m.group(1).split("@")[0].split("+")[0]))
    return found


def compare(old_dir, new_dir, externs=(), objdump="objdump"):
    if shutil.which(objdump) is None:
        raise CompareError("%s isn't there: binutils' objdump is needed" % objdump)
    old, new = read_folder(old_dir), read_folder(new_dir)
    if not old or not new:
        raise CompareError("no objects in %s" % (old_dir if not old else new_dir))
    sha = {side: {k: hashlib.sha256(v).hexdigest() for k, v in objs.items()}
           for side, objs in (("old", old), ("new", new))}
    same = sorted(k for k in old if k in new and sha["old"][k] == sha["new"][k])
    differ = sorted(k for k in old if k in new and sha["old"][k] != sha["new"][k])
    only_old = sorted(k for k in old if k not in new)
    only_new = sorted(k for k in new if k not in old)
    out = {"old": old_dir, "new": new_dir, "same": same, "differ": differ,
           "only_old": only_old, "only_new": only_new, "externs": sorted(externs)}
    if not differ and not only_old and not only_new:
        return out
    # A library's member that is byte for byte an object beside it is that
    # object: its functions are read once.
    copies = {side: {k for k in sha[side] if "(" in k and sha[side][k] in
                     {v for o, v in sha[side].items() if "(" not in o}}
              for side in ("old", "new")}
    out["copies"] = sorted(copies["old"] | copies["new"])
    tmp = tempfile.mkdtemp(prefix="compare_engines.")
    try:
        sides = {}
        for side, objs, keys in (("old", old, differ + only_old), ("new", new, differ + only_new)):
            keys = [k for k in keys if k not in copies[side]]
            syms, funcs = {}, collections.defaultdict(list)
            for key in keys:
                obj = Object(key, objs[key])
                for s in obj.defined():
                    letter = obj.sections[s.section].letter()
                    syms.setdefault(s.name, []).append(
                        (key, letter.upper() if s.glob else letter, s.size))
                for fname, body in disassemble(obj, objdump, tmp).items():
                    funcs[fname.split("#")[0]].append((key, body))
            sides[side] = (syms, funcs)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    out["symbols"] = _symbols(sides["old"][0], sides["new"][0])
    out["functions"] = _functions(sides, out["symbols"], set(externs))
    return out


def _symbols(old, new):
    """Each symbol whose object, type or size changed, or that is in one
    engine only, as (name, old (object, type, size) or None, new ...)."""
    rows = []
    for name in sorted(old.keys() | new.keys()):
        a, b = old.get(name, []), new.get(name, [])
        if len(a) <= 1 and len(b) <= 1:
            a, b = (a[0] if a else None), (b[0] if b else None)
            if a != b:
                rows.append((name, a, b))
            continue
        # A static name in several objects: pair the same objects first.
        a_left, b_left = list(a), list(b)
        for x in list(a_left):
            for y in b_left:
                if x[0] == y[0]:
                    a_left.remove(x)
                    b_left.remove(y)
                    if x != y:
                        rows.append((name, x, y))
                    break
        for x, y in zip(a_left, b_left):
            if x != y:
                rows.append((name, x, y))
        rows += [(name, x, None) for x in a_left[len(b_left):]]
        rows += [(name, None, y) for y in b_left[len(a_left):]]
    return rows


def _functions(sides, symbols, externs):
    (old_syms, old_funcs), (new_syms, new_funcs) = sides["old"], sides["new"]
    pairs, one_sided = [], []
    for name in sorted(old_funcs.keys() | new_funcs.keys()):
        a, b = list(old_funcs.get(name, [])), list(new_funcs.get(name, []))
        for x in list(a):             # the same object first, then the same text
            for y in b:
                if x[0] == y[0] or x[1] == y[1]:
                    a.remove(x)
                    b.remove(y)
                    pairs.append((name, x, y))
                    break
        for x, y in zip(a, b):
            pairs.append((name, x, y))
        one_sided += [(name, x, None) for x in a[len(b):]]
        one_sided += [(name, None, y) for y in b[len(a):]]
    kinds = []
    # What the move changed: a function in both engines that moved or went
    # extern, one only in one engine as a static or a compiler's copy (so
    # inlined on the other side), and the statics named as gone extern.
    changed = set(externs)
    for name, a, b in symbols:
        if a is not None and b is not None and (a[0] != b[0] or a[1] != b[1]):
            changed.add(base_name(name))
    for name, x, y in one_sided:
        entries = (old_syms if x else new_syms).get(name, [])
        local = all(e[1].islower() for e in entries) if entries else True
        if base_name(name) != name or local or base_name(name) in externs:
            changed.add(base_name(name))
            kinds.append((name, "only in one engine", x, y, None))
        else:
            kinds.append((name, "anything else", x, y, None))
    # What each function calls, on either side, so that what an inlined
    # body called counts with it.
    calls = collections.defaultdict(set)
    for funcs in (old_funcs, new_funcs):
        for name, copies in funcs.items():
            for _key, body in copies:
                calls[base_name(name)] |= callees(body)

    # Where each function is, on each side, to see where the move put a
    # file between a function and what it calls.
    homes = {}
    for side, syms in (("old", old_syms), ("new", new_syms)):
        homes[side] = collections.defaultdict(set)
        for sym, entries in syms.items():
            homes[side][base_name(sym)] |= {e[0] for e in entries}

    def explained(gained, x, y):
        """Whether what the caller calls on one side only is the move's: a
        callee the move changed, or one that was in the caller's file on
        one side and not on the other, and what those call in turn."""
        split = {g for g in gained
                 if (x[0] in homes["old"][g]) != (y[0] in homes["new"][g])}
        seen, todo = set(), [g for g in gained if g in changed or g in split]
        while todo:
            f = todo.pop()
            if f not in seen:
                seen.add(f)
                todo += calls.get(f, ())
        return gained <= changed | seen
    for name, x, y in pairs:
        a, b = x[1], y[1]
        if a != b and base_name(name) in changed:
            # A static gone extern can take its address now, and with
            # -fcf-protection GCC starts it with endbr64.
            a, b = a[a[:1] == ["endbr64"]:], b[b[:1] == ["endbr64"]:]
        if a == b:
            moved = x[0] != y[0] or base_name(name) in changed
            kinds.append((name, "moved or gone extern" if moved else "the same", x, y, None))
            continue
        gained = callees(a) ^ callees(b)
        if gained and explained(gained, x, y):
            kinds.append((name, "a call where a body was inlined", x, y, sorted(gained)))
        else:
            kinds.append((name, "anything else", x, y, sorted(gained)))
    kinds.sort(key=lambda k: k[0])
    return kinds


# ---------------------------------------------------------------- report

def _sym(entry):
    if entry is None:
        return "none"
    key, letter, size = entry
    return "%s %s %s" % (size if size is not None else "?", letter, key)


def report(out, write_dir=None):
    lines = ["Engine comparison: %s against %s" % (out["new"], out["old"])]
    lines.append("Objects: %d the same, %d differ, %d only in the old, %d only in the new"
                 % (len(out["same"]), len(out["differ"]), len(out["only_old"]),
                    len(out["only_new"])))
    if "functions" not in out:
        lines.append("Result: the same engine, every object byte for byte")
        return "\n".join(lines), 0
    for label, keys in (("differ", out["differ"]), ("only in the old", out["only_old"]),
                        ("only in the new", out["only_new"])):
        lines += ["  %s: %s%s" % (label, k, ", the object beside it" if k in out["copies"] else "")
                  for k in keys]
    if out["externs"]:
        lines.append("Named as gone extern (--extern): " + ", ".join(out["externs"]))
    counts = collections.Counter(k for _n, k, _x, _y, _c in out["functions"])
    lines.append("Symbols changed or in one engine: %d" % len(out["symbols"]))
    lines.append("Functions:")
    lines += ["  %-34s %d" % (k + ":", counts[k]) for k in KINDS]
    other = counts["anything else"]
    lines.append("Result: " + ("every difference is one a move makes" if not other else
                               "%d functions differ in a way a move doesn't (listed first)"
                               % other))
    funcs = out["functions"]

    def where(x, y):
        a, b = (x[0] if x else "none"), (y[0] if y else "none")
        return a if a == b else "%s -> %s" % (a, b)

    def diff(name, x, y):
        a = x[1] if x else []
        b = y[1] if y else []
        return ["    " + d.rstrip("\n") for d in difflib.unified_diff(
            a, b, "old " + name, "new " + name, n=2, lineterm="")]
    for kind in ("anything else", "a call where a body was inlined"):
        rows = [f for f in funcs if f[1] == kind]
        if not rows:
            continue
        lines += ["", "== %s (%d):" % (kind[0].upper() + kind[1:], len(rows))]
        for name, _k, x, y, calls in rows:
            note = " (calls on one side only: %s)" % ", ".join(calls) if calls else ""
            lines.append("  %s, %s%s" % (name, where(x, y), note))
            lines += diff(name, x, y)
    for kind in ("only in one engine", "moved or gone extern"):
        rows = [f for f in funcs if f[1] == kind]
        if rows:
            lines += ["", "== %s (%d):" % (kind[0].upper() + kind[1:], len(rows))]
            lines += ["  %s, %s" % (name, where(x, y)) for name, _k, x, y, _c in rows]
    if out["symbols"]:
        lines += ["", "== Symbols changed or in one engine (size, type, object; old -> new):"]
        lines += ["  %s: %s -> %s" % (n, _sym(a), _sym(b)) for n, a, b in out["symbols"]]
    if write_dir:
        _write(write_dir, funcs, diff)
    return "\n".join(lines), (1 if other else 0)


def _write(folder, funcs, diff):
    root = pathlib.Path(folder)
    root.mkdir(parents=True, exist_ok=True)
    for name, kind, x, y, _c in funcs:
        safe = re.sub(r"[^\w.+-]", "_", name)
        for side, entry in (("old", x), ("new", y)):
            if entry:
                (root / ("%s.%s.txt" % (safe, side))).write_text("\n".join(entry[1]) + "\n")
        if kind != "the same":
            (root / ("%s.diff" % safe)).write_text("\n".join(diff(name, x, y)) + "\n")


def main(argv):
    parser = argparse.ArgumentParser(
        prog="compare_engines.py",
        usage="%(prog)s OLD NEW [--extern NAME ...] [--objdump PROG] [--out DIR]",
        description="Whether two builds of the engine differ only as a move makes them.")
    parser.add_argument("old")
    parser.add_argument("new")
    parser.add_argument("--extern", action="extend", nargs="+", default=[])
    parser.add_argument("--objdump", default="objdump")
    parser.add_argument("--out")
    try:
        args = parser.parse_args(argv[1:])
    except SystemExit as e:
        return 2 if e.code else 0
    externs = [n for arg in args.extern for n in arg.split(",") if n]
    try:
        for folder in (args.old, args.new):
            if not os.path.isdir(folder):
                raise CompareError("%s is not a folder" % folder)
        out = compare(args.old, args.new, externs, args.objdump)
        text, code = report(out, args.out)
    except CompareError as e:
        print("compare_engines: %s" % e, file=sys.stderr)
        return 2
    print(text)
    return code


if __name__ == "__main__":
    sys.exit(main(sys.argv))
