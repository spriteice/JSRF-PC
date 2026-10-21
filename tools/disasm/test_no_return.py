"""A call that never returns ends its body.

JSRF starts its loader threads by address. Each routine ends by calling a
wrapper around PsTerminateSystemThread, and MSVC follows that call with int3,
then nop padding, then the next routine. Nothing stopped at the call: the
first routine's extent and descent ran on through the others, so they were
never in a gap, the immediate pass skipped them, and the runtime could not
resolve the threads it was asked to start. A routine that pops after the call
instead of trapping (JSRF's 0x00127F70) did not close as a callback either.

Fixtures are synthetic: hand-assembled bytes, no game data.
"""
import struct

from tools.disasm.test_jump_table_follower import BASE, _detect, _engine

EXIT, T1, T2, T3, NEXT = (BASE + off for off in (0x20, 0x30, 0x40, 0x50, 0x60))
SLOT = BASE + 0x1000                     # an import slot, outside the image


def _call(at, target):
    return b"\xe8" + struct.pack("<i", target - at - 5)


def _threads():
    """BASE:  push T1 ; push T2 ; push T3 ; call NEXT ; add esp, 12 ; ret
    EXIT:  push 0 ; call [SLOT] ; int3              the thread-exit wrapper
    T1:    inc eax ; push 1 ; call EXIT ; int3 ; nop padding
    T2:    dec eax ; push 2 ; call EXIT ; int3 ; nop padding
    T3:    push esi ; push 0 ; call EXIT ; pop esi ; nop padding
    NEXT:  xor eax, eax ; ret
    """
    code = bytearray(b"\xcc" * (NEXT - BASE + 16))

    def put(at, body):
        code[at - BASE:at - BASE + len(body)] = body

    put(BASE, b"".join(b"\x68" + struct.pack("<I", t) for t in (T1, T2, T3))
        + _call(BASE + 15, NEXT) + b"\x83\xc4\x0c\xc3")
    put(EXIT, b"\x6a\x00\xff\x15" + struct.pack("<I", SLOT) + b"\xcc")
    for at, op in ((T1, b"\x40\x6a\x01"), (T2, b"\x48\x6a\x02")):
        put(at, op + _call(at + 3, EXIT) + b"\xcc" + b"\x90" * 7)
    put(T3, b"\x56\x6a\x00" + _call(T3 + 3, EXIT) + b"\x5e" + b"\x90" * 7)
    put(NEXT, b"\x33\xc0\xc3")
    return bytes(code)


def test_routines_after_a_no_return_call_are_found():
    det, engine = _detect(_threads())
    assert EXIT in engine.no_return
    for routine in (T1, T2, T3):
        assert routine in det.functions, sorted(hex(a) for a in det.functions)
        # inc/dec or push esi, a push, and the call: nothing after it runs.
        assert det.functions[routine].end == routine + 8


def test_a_wrapper_is_no_return_only_when_no_path_returns():
    #  WRAP: push 0 ; call EXIT ; pop ecx ; ret     the ret is never reached
    #  COND: test eax, eax ; je +5 ; call EXIT ; ret
    wrap, cond = BASE + 0x30, BASE + 0x40
    code = bytearray(_threads()[:0x30] + b"\xcc" * 0x20)
    code[0x30:0x39] = b"\x6a\x00" + _call(wrap + 2, EXIT) + b"\x59\xc3"
    code[0x40:0x4a] = b"\x85\xc0\x74\x05" + _call(cond + 4, EXIT) + b"\xc3"
    engine, _ = _engine(bytes(code))
    assert engine.mark_no_return({EXIT: wrap, wrap: cond, cond: cond + 10})
    assert engine.no_return == {EXIT, wrap}
    reached = engine.recursive_descent([wrap], [(BASE, BASE + len(code))])
    assert wrap + 7 not in reached          # the pop ecx after the call
    assert EXIT in reached
