# Recompilation Toolchain

> **From the xboxrecomp toolkit,** by Ned Heller (sp00nznet), written for
> the toolkit's own work, much of it on Burnout 3: Takedown, on Windows with
> Direct3D 11. JSRF-PC keeps it for how the pipeline and runtime work. Where
> it differs from how JSRF-PC is built or changed,
> [building-on-linux.md](../docs/building-on-linux.md) and
> [project-rules.md](../docs/project-rules.md) are right, and this doc is out
> of date there: say so. The generated code is never edited, and a bug is
> fixed where it starts, not stubbed out or guarded around. The sections
> from "Reading a log: logsum" on are JSRF-PC's own.

Python tools that transform an Xbox executable (XBE) into compilable C source code. Run them in sequence — each tool's output feeds the next.

## Prerequisites

```bash
pip install capstone==5.0.9    # x86 disassembly engine, the version CI tests with
```

Python 3.10+ required. On Windows, use `py -3` instead of `python3`.

## Pipeline Overview

JSRF-PC runs five of these tools, with abi_analysis between func_id and
recomp, and with options this game needs that the short commands below leave
out: run the commands in
[building-on-linux.md, step 4](../docs/building-on-linux.md#steps).

```
default.xbe
    │
    ▼
┌─────────────────────────────────────┐
│  1. xbe_parser                       │  Parse XBE headers, sections, imports
│     py -3 -m tools.xbe_parser        │  Output: section map, kernel imports
│     game_files/default.xbe           │
└─────────────┬───────────────────────┘
              │
              ▼
┌─────────────────────────────────────┐
│  2. disasm                           │  Disassemble .text, find functions
│     py -3 -m tools.disasm            │  Output: functions.json, xrefs.json
│     game_files/default.xbe           │
│     --text-only                      │
└─────────────┬───────────────────────┘
              │
              ▼
┌─────────────────────────────────────┐
│  3. func_id                          │  Classify: CRT, RenderWare, XDK, game
│     py -3 -m tools.func_id           │  Output: classified function database
│     game_files/default.xbe -v        │
└─────────────┬───────────────────────┘
              │
              ▼
┌─────────────────────────────────────┐
│  4. recomp                           │  Translate x86 → C source code
│     py -3 -m tools.recomp            │  Output: gen/*.c, dispatch, headers
│     game_files/default.xbe           │
│     --all --split 1000               │
└─────────────┬───────────────────────┘
              │
              ▼
        gen/recomp_*.c
        gen/recomp_dispatch.c
        gen/recomp_funcs.h
        gen/recomp_stubs.c
```

## Tool Details

### 1. xbe_parser — XBE File Parser

Reads an Xbox executable and extracts all metadata needed by downstream tools.

```bash
py -3 -m tools.xbe_parser game_files/default.xbe
```

**What it extracts:**
- Base address (always 0x00010000 for retail XBEs)
- Entry point (XOR-decoded from header)
- XDK version (build number)
- Certificate info (title name, allowed media types)
- Section table (name, VA, size, raw offset for each section)
- Kernel thunk table (ordinal imports)
- TLS directory
- Library versions (statically linked XDK libs)

**Output format:** Human-readable text to stdout. Key values for your project:

```
Entry Point:   0x001D2807
XDK Version:   5849
Sections:
  .text    VA=0x00011000  Size=2863616  RawOff=0x1000
  .rdata   VA=0x0036B7C0  Size=289684   RawOff=0x2BD7C0
  .data    VA=0x003B2360  Size=3904988  RawOff=0x302360
  ...
Kernel Imports: 147 ordinals
```

Use these values to configure `xbox_memory_layout.h` in your runtime.

---

### 2. disasm — Disassembler & Function Detector

Performs static analysis on the .text section to find all functions.

```bash
py -3 -m tools.disasm game_files/default.xbe --text-only
```

**Options:**
| Flag | Description |
|------|-------------|
| `--text-only` | Only analyze .text section (skip library sections) |
| `-o` / `--output DIR` | Output directory (default: `tools/disasm/output/`) |
| `--verbose` / `-v` | Show progress during analysis |

**How it works:**
1. **Linear sweep** — disassembles every byte, looking for function prologues (`push ebp; mov ebp, esp` or `sub esp, N`)
2. **Recursive descent** — follows call/jump targets to discover more functions
3. **Cross-reference analysis** — builds call graph, identifies callers/callees
4. **String detection** — finds ASCII/Unicode strings referenced by code

**Output files:**
| File | Contents |
|------|----------|
| `functions.json` | Array of `{address, size, instructions, calls, callers}` |
| `xrefs.json` | Cross-reference database (call graph) |
| `strings.json` | Discovered string references |
| `stats.json` | Summary statistics |

**Function detection accuracy:**
- Typically finds 95%+ of functions on first pass
- Remaining ~5% are callback functions only reachable via indirect calls
- The recompiler handles unknown targets at runtime via the ICALL dispatch

---

### 3. func_id — Function Identifier

Classifies functions into categories to help you understand what you're looking at.

```bash
py -3 -m tools.func_id game_files/default.xbe -v
```

**Options:**
| Flag | Description |
|------|-------------|
| `-v` / `--verbose` | Show identification details |
| `-o` / `--output DIR` | Output directory (default: `tools/func_id/output/`) |

**Classification strategies:**

| Strategy | What It Detects |
|----------|----------------|
| **CRT identifier** | C runtime: malloc, free, memcpy, strcpy, printf, math functions |
| **RW identifier** | RenderWare engine: RwCamera*, RpWorld*, RwTexture*, RwStream* |
| **Immediate scanner** | Functions that reference known constants (vtable addresses, magic numbers) |
| **Vtable scanner** | Virtual method tables (arrays of function pointers) |
| **Stub classifier** | Empty/trivial functions (ret, xor eax,eax; ret) |
| **Clustering** | Groups similar functions by instruction patterns |

**Output:** Annotated function database with categories:

```
0x00011000  CRT     _mainCRTStartup
0x00011240  GAME    resource_loader
0x000636D0  GAME    physics_force_apply
0x001DD910  RW      RwCameraBeginUpdate
0x0034D530  RW      renderware_main_render (79KB!)
```

**Why this matters:** Knowing which functions are CRT vs RenderWare vs game code helps you prioritize. CRT functions usually "just work" in recompiled form. RenderWare functions may need manual overrides. Game functions need the most attention.

---

### 4. recomp — x86 to C Static Recompiler

The core tool. Translates every x86 instruction in every function into equivalent C code.

```bash
py -3 -m tools.recomp game_files/default.xbe --all --split 1000
```

**Options:**
| Flag | Description |
|------|-------------|
| `--all` | Recompile all detected functions |
| `--split N` | Split output into files of N functions each |
| `--split-bytes BYTES` | With `--split`, a file also ends once its code reaches BYTES, so the files are alike in size and a compile on every core ends sooner (JSRF-PC: `1000000`) |
| `--jobs N` | With `--split`, translate on N processes, each loading the data itself (about 0.7 GB each for a whole game); the files are the same for any N. Default 1 |
| `--gen-dir DIR` | Where the split files go (JSRF-PC: `port/recomp/gen`) |
| `--exclude-manual FILE` | Leave out the functions FILE defines by hand (JSRF-PC: `port/recomp_manual.c`) |
| `--back-edge-yield` | Let a guest thread busy in a loop hand the CPU to the others |
| `-f` / `--function ADDR` | Recompile a single function (hex address) |
| `-o` / `--output-dir DIR` | Where the run's summary goes (default: `tools/recomp/output/`) |
| `-v` / `--verbose` | Show per-function progress |

**Output files:**
| File | Contents |
|------|----------|
| `gen/recomp_0000.c` ... `recomp_NNNN.c` | Recompiled function bodies (split by --split) |
| `gen/recomp_dispatch.c` | Binary-search dispatch table (ICALL resolution) |
| `gen/recomp_funcs.h` | Forward declarations for all recompiled functions |
| `gen/recomp_stubs.c` | Stub functions for unresolvable targets |

**How it translates:**

```asm
; Original x86                    ; Generated C
push ebp                          PUSH32(esp, g_seh_ebp);
mov ebp, esp                      ebp_local = esp;
sub esp, 0x10                     esp -= 0x10;
mov eax, [ebp+8]                  eax = MEM32(ebp_local + 8);
add eax, [ebp+0xC]                eax += MEM32(ebp_local + 0xC);
mov [ebp-4], eax                  MEM32(ebp_local - 4) = eax;
mov esp, ebp                      esp = ebp_local;
pop ebp                           POP32(esp, g_seh_ebp);
ret                               esp += 4; return;
```

**What it handles:**
- All standard x86 instructions (MOV, ADD, SUB, CMP, TEST, JCC, CALL, RET, etc.)
- x87 FPU (FADD, FMUL, FSTP, etc.) via C `float`/`double`
- SSE/SSE2 (MOVSS, ADDSS, MULSS, COMISS, etc.) via C `float`
- MMX (basic operations, packed byte/word/dword)
- String operations (REP MOVSB, CMPSB, STOSB, SCASB)
- Indirect calls → RECOMP_ICALL() macro dispatch
- Switch/jump tables → C switch statements
- Stack frame simulation (PUSH/POP via simulated ESP)

**What needs manual attention:**
- **Indirect calls** — 90% resolve automatically via the dispatch table. The other 10% need investigation (corrupted vtables, function pointer arrays).
- **Self-modifying code** — not supported (Xbox games rarely use this).
- **Inline assembly** — translated mechanically, but semantics may need review.
- **Large functions** — very large functions (50KB+) may have issues with branch targets spanning too far. The recompiler handles this, but edge cases exist.

---

## After Recompilation

In JSRF-PC the generated C files go in `port/recomp/gen`, and
[port/CMakeLists.txt](../port/CMakeLists.txt) builds them with the runtime in
`src/` and the hand-written code in `port/`: with GCC or Clang on Linux, and
with llvm-mingw for Windows, cross-compiled on Linux.
[docs/building-on-linux.md](../docs/building-on-linux.md) has the steps. A
crash or a wrong result is found as
[docs/pipeline/06-debugging.md](../docs/pipeline/06-debugging.md) says, and
fixed where it starts ([docs/project-rules.md](../docs/project-rules.md),
Where code goes).

The generated code is intentionally verbose and mechanical — it's not meant to be pretty, it's meant to be correct. Each function maps 1:1 to the original Xbox binary.

## Regeneration

The generated files are never edited by hand
([docs/project-rules.md](../docs/project-rules.md), Where code goes 3), so
making them again loses nothing. A function written by hand lives in `port/`
(Where code goes 6), and `--exclude-manual port/recomp_manual.c` tells the
recompiler which functions those are, so it leaves their bodies out. A bug in
the generated code is fixed in the recompiler, and the code is made again.

The recompiler also copies `templates/runtime/recomp_types.h`, the runtime's
register model, into the generated code, and the engine compiles against that
copy. [port/CMakeLists.txt](../port/CMakeLists.txt) refuses to build with a
copy that isn't the tree's header, so after a change to it the code is made
again too.

## Reading a log: logsum

`tools/logsum.py` turns JSRF-PC's logs into tables of how the game ran, one
row for each of the engine's reports, about every 10 s: frames, frames late
and held, the worst frame, draws and pushbuffer words a frame, sounds
started, the game and GPU threads' share of a core, and the presenter's and
the display's counts where the log has them. Each run in a log gets its own
table, its settings and Developer switches, and its frames over 50 ms.

```bash
python3 tools/logsum.py last-run.log [more.log ...] > summary.md
python3 tools/logsum.py --json last-run.log [more.log ...] > summary.json
```

With `--json` it prints the same for a program: each log's runs, each with
its header (the version, `linux` or `windows`, the settings and Developer
switches), every report's values by name (`frames`, `late`, `late_pct`,
`held`, `held30`, `worst_ms`, `draws_per_frame`, `kwords_per_frame`,
`voices`, the game and GPU threads, the GPU thread's split, the card, the
presenter and the display), the run's totals, the finish line's counts and
the frames over 50 ms, as numbers, with null where the log has no line.
`worst_ms` is null when no frame took 25 ms or longer.

It needs only Python's standard library. Its test, `tools/test_logsum.py`,
takes each line it reads from the engine's own source, so a log line changed
in the engine fails that test until logsum is changed to match.

## Reading a log in full: logdigest

`tools/logdigest.py` turns a log into a digest meant to be read whole: a
log over 100 KB is read in full through it, and then where it points
([docs/project-rules.md](../docs/project-rules.md), Workflow 5).

```bash
python3 tools/logdigest.py last-run.log [more.log ...] > digest.md
python3 tools/logdigest.py last-run.log --against old-last-run.log > digest.md
python3 tools/logdigest.py --tag FILE last-run.log    # every kind of [FILE] line
```

For each run in a log (from the engine's banner, one per start; the lines
before the first are a part of their own):

- **The header:** the version, the system and Developer switches (as logsum
  reads them), the first line, the size, and how the run ended: a clean
  exit (the window closed, then the renderer's report), a crash report, a
  restart, or cut off with its last line, and any freeze report.
- **Every line counted by its tag** (`[GL]`, `[PACE]`: the line's start
  after spaces, or "(no tag)"), with each tag's first and last line. The
  counts add up to the run's lines, and the digest says so.
- **Every error, check, crash and freeze line in full,** with the line
  before and after it, grouped as the tool's `ERRORS` list says. Past 100
  lines in a group, its first and last 50 with the count between, and
  `--tag` for all of them. A crash report is given whole to the run's end
  (up to 200 lines, then its last 20), and a freeze report, which repeats
  every 10 s, as its first and its last. Lines with an error word that
  aren't errors, such as a failed open of the game's cache before the disc,
  are in `NOT_ERRORS`, each with why.
- **The first of each kind of line,** with its line number and count. A
  kind is the line with its numbers, lists of numbers and paths made one
  (`#`, `<path>`): the first 20 kinds of each tag, then how many more. A
  later run in the same log lists only the kinds no earlier run had.

With several logs it lists what only one of them has, and with
`--against` an older log the tags and kinds new since it and gone since
it. Line numbers are those `sed -n` gives; a CR-LF log, a last line with no
end and bytes that aren't UTF-8 read like any other. It needs only Python's
standard library, and logsum beside it. A log of a million lines gives a
digest under 64 KB; the logs of November 2026 give 16 to 50 KB, and a 145 MB
log takes about 45 s.

Its test, `tools/test_logdigest.py`, writes its logs from the engine's own
lines, as logsum's does. It also reads every format the engine prints (each
call of fprintf, printf, guard_say and fputs, and each string that starts
with a tag): every line among them with an error word (fail, error, cannot,
could not, refused, crash, frozen, fatal, corrupt, bad, unable, abort) must
be in `ERRORS` or `NOT_ERRORS`, and not in both, so a new error line in the
engine fails the test until the tool has it.

## Holding logs to numbered checks: logcompare

`tools/logcompare.py` holds one build's logs to a row's numbered checks,
before against after: each check's before, its after, and PASS or FAIL. It
sits beside logdigest, which finds new kinds of line; this finds numbers
that moved.

```bash
python3 tools/logcompare.py tools/checks/finish-line.ini after.log [more.log ...] > checks.md
python3 tools/logcompare.py tools/checks/finish-line.ini after.log --before before.log [more.log ...]
```

A checks file is INI, in `tools/checks/`, one section a check named by its
number as the row numbers it, with `#` for comments:

```ini
[3]
name = No frame over 50 ms on entering a level
line = [HITCH] frame {}: {ms} ms flip to flip
from = [LOAD] t={} mission ... asks for stage
take = max
limit = <= 50
```

- **What it reads:** `line`, a template of a line the engine prints: its
  text, `{name}` for a number taken from it, `{}` for a number not used,
  `...` for any text, and a run of spaces for any run of spaces, matched
  from the line's start (the spaces before it ignored); a line worded two
  ways gives a template a line. `measure` names the number, where the
  template has more than one. Or `measure = report KEY`, one of logsum's
  values (`late_pct`, `held`, `worst_ms` and the rest), for each full
  10 s report, the first and last of each run left out as logsum's finish
  line leaves them out.
- **What it takes** over each log: `take` is max, min, mean, sum, count,
  first, last or a percentile (`p95`, the nearest rank). `skip = N` leaves
  out the first N matched in each run, and `from` and `until`, templates,
  open and close the part of each run counted (one stage's `[LOAD]` line
  to the next; those lines aren't counted, and a line that matches both
  opens). `device = linux` or `windows` counts only that device's logs, as
  logsum tells them apart.
- **The before:** `before`, a number or a range (`65..71`), today's figure;
  logs given with `--before`, such as five replays of a saved run, take its
  place as a range, the spread with no change to the code.
- **The limit:** `<`, `<=`, `>`, `>=` or `=` and a number, or (but `=`)
  `before`, `before + N`, `before - N` or `before * N`, where against a
  range `before` is its edge on the limit's side: `<= before` is no
  higher than the highest.

A check passes when every after log of its device passes. One with no
line in a log has no value there (a count is 0), and the table says so.
Under the table, each value is traced to the log's line, or to the lines it
was taken over, so a figure can be checked before it is leaned on. Its exit
status is 0 when every check measured passes, 1 when one fails, has no line
or has no before its limit needs, and 2 when it can't run.

It needs only Python's standard library, and logsum beside it. Its test,
`tools/test_logcompare.py`, writes its logs from the engine's own lines, as
logsum's does, and holds every template in `tools/checks/` to the engine's
source: its fixed text must be a line the engine prints, with a number
where the engine prints one (`%d`, `%u`, `%f` and the like), so a change
that rewords a line a check reads fails until the check changes with it.
`tools/checks/finish-line.ini` is the performance review's finish line, as
the b3.1 plan proposes it.

## Which generated functions a change alters: gen_diff

`tools/gen_diff.py` compares two folders of generated code made from
the same default.xbe, the kept copy and the one a change to `tools/` or
`port/recomp_manual.c` made, function by function:

```bash
python3 tools/gen_diff.py KEPT NEW
```

Each function is found by its definition in whichever file holds it, and
compared by its code, with its comments and spacing left out. It prints
counts and addresses only, never the code or a function's name, so its
report can go in a build's TESTING.md: how many functions are the same,
changed, changed only in their comments, renamed, added and removed, the
addresses of those changed, renamed, added and removed, and how many
declarations and tables outside the functions differ, by file. A function
is known by the address in its name, its banner's `Original:` line, or its
dispatch-table entry, and one with none of those is counted but never
listed. One found under another name at the same address, as
`sub_XXXXXXXX` becomes `sub_XXXXXXXX_gen` when `port/recomp_manual.c` wraps
it, is renamed, not removed and added; if its code changed too, apart from
its name, it counts as changed.
docs/build-and-test-reference.md (What is kept between builds) says when it
runs. It needs only Python's standard library, and its test,
`tools/test_gen_diff.py`, runs on folders it makes up.

## Whether moved code arrived as it left: move_check

`tools/move_check.py` checks a change that moves code out of one file or
several into others, a unit of its own or a file already there: the lines
taken out of each file must be the lines put into the units, word for word
and in the same order.

```bash
python3 tools/move_check.py BASE [NEW] --from FILE [FILE ...] --to UNIT [UNIT ...] [--rename OLD=NEW ...]
```

BASE and NEW are two trees git knows, a commit or a tree hash; without NEW
it checks what is staged, and prints its tree hash. With several units,
each is matched in the order given against the lines not yet matched, from
the files it shares the most lines with first. A
moved line may differ only by a `static` taken off its start (a static the
files now share, gone extern), or by a name given its unit's prefix on the
way (`--rename OLD=NEW`, as a whole word, which also covers the lines that
change only by it where they are). A new unit's head comment, and
`#include` lines, blank lines and declarations (prototypes and `extern`
lines) in the files and the units, are counted and need no word. Everything
else in them is listed, marked `!`, for the review pack to explain: lines
taken out that arrived nowhere as they were, lines in a unit that weren't
moved (an accessor, say), lines added to a file, and comments changed.
The other files the change touches (a header, CMakeLists.txt, a test's
run.py) are listed by file, with their lines sorted the same way, but don't
decide the result. Its exit status is 0 when every line in the files and the
units is accounted for, 1 when some are listed to explain, and 2 when it
can't run. It needs only Python's standard library and git, and its test,
`tools/test_move_check.py`, runs on trees it makes up.

## Whether two engines differ only as a move makes them: compare_engines

`tools/compare_engines.py` compares two builds of the engine, from a
change's base tree and from its tree, built the same way with each checkout
at the same path, as docs/build-and-test-reference.md (The same engine)
builds them:

```bash
python3 tools/compare_engines.py OLD NEW [--extern NAME ...] [--objdump PROG] [--out DIR]
```

OLD and NEW are the two build folders. Every object and static library
under each is read (`*.o`, `*.obj` and `*.a`), Linux's and Windows'. Objects
with the same SHA-256 are the same, and when every one is, it says so and
stops. Otherwise it lists the symbols of the objects that differ whose size,
type (`t` to `T`, a static gone extern) or object changed, or that are in
one engine only (Windows' objects have no sizes, so there a function runs
to the next symbol), and compares every function by its name, wherever it
now is, disassembled by binutils' objdump with the addresses left out. A
reference is named by what it points at, a function or variable by its
name, a string by its text and a constant by its bytes, with a field in
those bytes that is relocated itself (a switch table's entry, a pointer)
named by what it points at, and a jump inside the function by how many
instructions away it lands, so the new order of a file's strings, statics
and code doesn't show. The nops that align code are left out, and a
function's address loaded from the GOT reads as the `lea` that takes it in
its own file. Each function is sorted: the same; the same but moved or
gone extern (with `-fcf-protection`, a static gone extern starts with
`endbr64`); different where it calls, on one side only, a function the
move changed, or one the move put a file between it and, and what those
call in turn (a body inlined on one side, a call on the other), with those
callees named; only in one engine, as a static or a
compiler's copy (`.constprop`, `.isra`, `.part`, `.cold`) inlined on the
other side, or as a function named with `--extern` (a static the move made
extern, which move_check lists); and anything else. Every function that
differs is shown with its diff. The sorting names a cause and doesn't prove
one, and the comparison shows where the code changed, not what the game
does. Data is compared by its symbols only. Its exit status is 0 when
nothing differs or everything that does is of a move's kinds, 1 when
anything else does, and 2 when it can't run; `--out` also writes each
function's text and each diff to a folder. It needs only Python's standard
library and objdump (the `linux tests` image has it, and it reads Windows'
objects too), and its test, `tools/test_compare_engines.py`, compiles
made-up files with GCC and with clang for Windows.
