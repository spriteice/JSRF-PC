"""A Disassembler decodes each instruction once, and its walks are unchanged.

tools.disasm's callback probes and tools.recomp's passes walk the same bytes
3 to 4 times, and decoding with Capstone's detail was most of the first
start's disassembly and translation. A Disassembler now keeps each decode,
keyed by the address and the bytes an instruction there can span, and hands
it to every later walk.

The walks must stay what Capstone's decode of the caller's window gives:
an instruction that runs past the window's end doesn't decode, however long a
window decoded it before, and other bytes at the same address are another
instruction. The reference walks below are the code as it was before the
cache, decoding every walk from scratch.
"""
import random

import pytest

from .disasm import Disassembler

BASE = 0x00012000

# +0 push ebp; +1 mov ebp, esp; +3 mov eax, [ebp+8]; +6 cmp eax, 3;
# +9 ja +18; +11 jmp [eax*4 + 0x12040]; +18 mov ecx, 0x11223344;
# +23 call 0x13000; +28 jmp +18; +30 lea esi, [esi]; +33 fld dword [eax];
# +35 ret 4
CODE = bytes.fromhex(
    "55" "8bec" "8b4508" "83f803" "7707" "ff248540200100"
    "b944332211" "e8e40f0000" "ebf4" "8d7600" "d900" "c20400")


def _reference_function(code, start_va, end_va, resync=None):
    d = Disassembler()
    size = end_va - start_va
    if size <= 0 or size > len(code):
        return []
    decoded = {}
    for cs_insn in d._cs.disasm(code[:size], start_va):
        decoded[cs_insn.address] = d._decode_instruction(cs_insn)
    for point in sorted(resync or ()):
        if not (start_va <= point < end_va) or point in decoded:
            continue
        for addr in [a for a, i in decoded.items() if a < point < a + i.size]:
            del decoded[addr]
        for cs_insn in d._cs.disasm(code[point - start_va:size], point):
            if cs_insn.address in decoded:
                break
            for inner in range(cs_insn.address + 1,
                               cs_insn.address + cs_insn.size):
                decoded.pop(inner, None)
            decoded[cs_insn.address] = d._decode_instruction(cs_insn)
    return [decoded[a] for a in sorted(decoded)]


def _reference_cfg(code, start_va, end_va, entry_points, stop_addresses=None,
                   stop_mnemonics=(), no_return=()):
    d = Disassembler()
    size = end_va - start_va
    if size <= 0 or size > len(code):
        return []
    decoded = {}
    stop_addresses = set(stop_addresses or ())
    worklist = list(entry_points)
    while worklist:
        entry = worklist.pop()
        if not (start_va <= entry < end_va) or entry in decoded:
            continue
        for cs_insn in d._cs.disasm(code[entry - start_va:size], entry):
            if cs_insn.address != entry and cs_insn.address in stop_addresses:
                break
            if cs_insn.address >= end_va or cs_insn.address in decoded:
                break
            insn = d._decode_instruction(cs_insn)
            decoded[insn.address] = insn
            if (insn.is_cond_jump and insn.jump_target is not None
                    and start_va <= insn.jump_target < end_va):
                worklist.append(insn.jump_target)
            if insn.is_jump:
                if (insn.jump_target is not None
                        and start_va <= insn.jump_target < end_va):
                    worklist.append(insn.jump_target)
                break
            if insn.is_ret or insn.mnemonic in stop_mnemonics:
                break
            if insn.is_call and insn.call_target in no_return:
                break
    return [decoded[a] for a in sorted(decoded)]


def _counting(monkeypatch):
    """A Disassembler, and the number of instructions it has decoded."""
    d = Disassembler()
    count = [0]
    decode = d._decode_instruction

    def counted(cs_insn):
        count[0] += 1
        return decode(cs_insn)

    monkeypatch.setattr(d, "_decode_instruction", counted)
    return d, count


def _random_code(seed, n):
    rng = random.Random(seed)
    # Mostly one-byte opcodes and modrm forms, so streams run a while before
    # an invalid byte stops them, with a few long immediates to cut.
    pool = bytes.fromhex("8b 89 01 03 0f 66 c7 e8 e9 eb 74 75 ff 50 58 90 c3"
                         " 44 24 08 00 b8 d9 dd 83 81")
    return bytes(rng.choice(pool) for _ in range(n))


def test_function_walks_match_capstone_at_every_end():
    # One Disassembler over every window, longest first: a window that cuts
    # an instruction must not get the whole one decoded for a longer window.
    d = Disassembler()
    for end in range(len(CODE), 0, -1):
        assert (d.disassemble_function(CODE, BASE, BASE + end)
                == _reference_function(CODE, BASE, BASE + end)), end
    for end in range(1, len(CODE) + 1):
        assert (d.disassemble_function(CODE, BASE, BASE + end)
                == _reference_function(CODE, BASE, BASE + end)), end


def test_resync_walks_match_capstone():
    d = Disassembler()
    for point in range(BASE, BASE + len(CODE)):
        for end in (len(CODE), len(CODE) - 2, 20):
            assert (d.disassemble_function(CODE, BASE, BASE + end, resync=[point])
                    == _reference_function(CODE, BASE, BASE + end, resync=[point]))


def test_cfg_walks_match_capstone():
    d = Disassembler()
    for entry in range(BASE, BASE + len(CODE)):
        for end in (len(CODE), len(CODE) - 2, 20):
            for kw in ({}, {"stop_addresses": {BASE + 23}},
                       {"no_return": {0x13000}}, {"stop_mnemonics": ("fld",)}):
                assert (d.disassemble_cfg(CODE, BASE, BASE + end, [entry], **kw)
                        == _reference_cfg(CODE, BASE, BASE + end, [entry], **kw))


@pytest.mark.parametrize("seed", range(4))
def test_walks_over_arbitrary_bytes_match_capstone(seed):
    code = _random_code(seed, 160)
    d = Disassembler()
    rng = random.Random(seed)
    for _ in range(60):
        start = rng.randrange(0, 120)
        end = rng.randrange(start + 1, len(code) + 1)
        sub, va = code[start:], BASE + start
        points = [va + rng.randrange(0, end - start) for _ in range(2)]
        assert (d.disassemble_function(sub, va, BASE + end, resync=points)
                == _reference_function(sub, va, BASE + end, resync=points))
        assert (d.disassemble_cfg(sub, va, BASE + end, points)
                == _reference_cfg(sub, va, BASE + end, points))


def test_an_instruction_cut_by_the_window_does_not_decode():
    # mov ecx, 0x11223344 at +18, five bytes: a window to +22 cuts it.
    d = Disassembler()
    whole = d.disassemble_cfg(CODE, BASE, BASE + len(CODE), [BASE + 18])
    assert whole[0].mnemonic == "mov" and whole[0].size == 5
    assert d.disassemble_cfg(CODE, BASE, BASE + 22, [BASE + 18]) == []
    assert d.disassemble_function(CODE[18:], BASE + 18, BASE + 22) == []


def test_other_bytes_at_the_same_address_decode_again():
    d = Disassembler()
    first = d.disassemble_function(b"\x40\xc3", BASE, BASE + 2)
    second = d.disassemble_function(b"\x48\xc3", BASE, BASE + 2)
    assert [i.mnemonic for i in first] == ["inc", "ret"]
    assert [i.mnemonic for i in second] == ["dec", "ret"]


def test_each_instruction_is_decoded_once(monkeypatch):
    d, count = _counting(monkeypatch)
    insns = d.disassemble_function(CODE, BASE, BASE + len(CODE))
    assert count[0] == len(insns)
    again = d.disassemble_function(CODE, BASE, BASE + len(CODE))
    d.disassemble_cfg(CODE, BASE, BASE + len(CODE), [BASE])
    assert count[0] == len(insns)
    assert all(a is b for a, b in zip(insns, again))
    # The same bytes cut out of a bigger buffer, behind seven int3s: only
    # the int3s are new.
    d.disassemble_function(b"\xcc" * 7 + CODE + b"\xcc", BASE - 7,
                           BASE + len(CODE))
    assert count[0] == len(insns) + 7


def test_translating_leaves_the_kept_decodes_as_they_were():
    # The translator's passes take their instructions from the decoder's
    # cache, so a change one made to an instruction would reach every later
    # walk. Translate a function, then decode each kept instruction afresh.
    from . import config
    from .translator import FunctionTranslator

    # +0 push esi; +1 mov esi, ecx; +3 test esi, esi; +5 je +23;
    # +7 loop: mov eax, [esi+4]; +10 fld dword [eax]; +12 fstp dword [esi+8];
    # +15 dec dword [esi]; +17 jne +7; +19 call +29; +24 pop esi; +25 ret;
    # +26 int3 x3; +29 ret
    code = bytes.fromhex(
        "56" "8bf1" "85f6" "7411" "8b4604" "d900" "d95e08" "ff0e" "75f4"
        "e803000000" "5e" "c3" "cccccc" "c3")
    config._install(
        [config.Section(".text", BASE, len(code), 0, len(code), True)],
        entry_point=BASE, kernel_thunk_addr=BASE, origin="decode-once-test")
    db = {va: {"start": f"0x{va:08X}", "end": va + size, "_addr": va,
               "size": size}
          for va, size in ((BASE, 26), (BASE + 29, 1))}
    ft = FunctionTranslator(code, db)
    assert ft.translate_function(BASE, db[BASE])
    kept = dict(ft.disasm._decoded)
    assert len(kept) >= 12
    fresh = Disassembler()
    for (addr, window), insn in kept.items():
        assert insn == fresh._decode_at(window, addr, len(window), addr), hex(addr)
