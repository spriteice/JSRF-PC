"""
Run kernel bridge bodies, compiled verbatim from kernel_bridge.c.

Run: python3 -m pytest tools/kernel_audit/test_bridge_bodies.py

test_bridge_ordinals.py checks the routing and stack-size tables; it cannot
see a bridge that reads the wrong argument or never writes its result. Each
body here is cut out of the source with the macros it uses and run against a
small guest memory, so the test follows the bridge as it is edited. Skips
without a C compiler.

NtProtectVirtualMemory (204) and KeSetDisableBoostThread (144) were unbridged
in JSRF. The fallback popped the right number of bytes and returned 0, but
VirtualProtect's *OldProtect was never written.
"""

import os
import re
import subprocess
import sys

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", ".."))

from tools.recomp import c_fixture  # noqa: E402

BRIDGE_C = os.path.join(os.path.dirname(__file__), "..", "..",
                        "src", "kernel", "kernel_bridge.c")

_PRELUDE = r"""
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
static uint32_t guest[0x400];
uint32_t g_eax, g_esp;
ptrdiff_t g_xbox_mem_offset;
static size_t xbox_GetMappedSize(void) { return sizeof guest; }
"""

_MAIN = r"""
int main(void)
{
    g_xbox_mem_offset = (ptrdiff_t)(uintptr_t)guest;
    g_esp = 0x100;

    /* VirtualProtect(buf, size, PAGE_READONLY, &old) with old zeroed first */
    BRIDGE_MEM32(0x100) = 0x180;
    BRIDGE_MEM32(0x104) = 0x184;
    BRIDGE_MEM32(0x108) = 0x02;
    BRIDGE_MEM32(0x10C) = 0x188;
    BRIDGE_MEM32(0x188) = 0;
    bridge_NtProtectVirtualMemory();
    if (g_eax != 0 || BRIDGE_MEM32(0x188) != 0x04) return 1;

    BRIDGE_MEM32(0x10C) = 0;
    bridge_NtProtectVirtualMemory();
    if (g_eax != 0xC0000005u) return 2;

    g_eax = 0xFFFFFFFFu;
    bridge_KeSetDisableBoostThread();
    if (g_eax != 0) return 3;
    return 0;
}
"""

_FAILURES = {
    1: "NtProtectVirtualMemory did not succeed with *OldProtect = PAGE_READWRITE",
    2: "NtProtectVirtualMemory took a null OldProtect",
    3: "KeSetDisableBoostThread did not report boosts as not disabled",
}


def _cut(src, pattern, flags=re.M):
    m = re.search(pattern, src, flags)
    assert m, f"could not locate {pattern!r} in kernel_bridge.c"
    return m.group(0)


def _function(src, name):
    return _cut(src, r"^static \w+ %s\([^)]*\)\n\{.*?\n\}\n" % name,
                re.M | re.S)


def test_protect_and_boost_bridges(tmp_path):
    cc = c_fixture.compiler()
    if not cc:
        pytest.skip("C compiler unavailable")
    with open(BRIDGE_C, encoding="utf-8", errors="replace") as fh:
        src = fh.read()
    source = "\n".join([
        _PRELUDE,
        _cut(src, r"^#define BRIDGE_MEM32\(.*$"),
        _cut(src, r"^#define STACK_ARG\(.*$"),
        _function(src, "bridge_out_ok"),
        _function(src, "bridge_NtProtectVirtualMemory"),
        _function(src, "bridge_KeSetDisableBoostThread"),
        _MAIN,
    ])
    c, exe = tmp_path / "bridges.c", tmp_path / "bridges.exe"
    c.write_text(source)
    built = subprocess.run(c_fixture.command(cc, [c], exe), cwd=tmp_path,
                           capture_output=True, text=True)
    assert built.returncode == 0, built.stdout + built.stderr
    code = subprocess.run([str(exe)], capture_output=True, timeout=10).returncode
    assert code == 0, _FAILURES.get(code, f"exit {code}")
