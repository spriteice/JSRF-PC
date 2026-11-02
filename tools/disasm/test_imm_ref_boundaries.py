"""Weak immediate references must not split an existing instruction stream."""
import struct
from tools.disasm.test_decode_at import _engine, BASE
from tools.disasm.functions import FunctionDetector

def detector(tail, target_offset):
    data = b'\xb8' + struct.pack('<I', BASE + target_offset) + b'\xc3' + b'\x90'*4 + tail
    engine, section = _engine(BASE, data)
    engine.image.read_bytes_at_va = lambda addr, size: data[addr-BASE:addr-BASE+size]
    engine.image.read_u32_at_va = lambda addr: (
        struct.unpack_from('<I', data, addr-BASE)[0]
        if 0 <= addr-BASE <= len(data)-4 else None)
    for insn in engine._cs.disasm(data, BASE):
        engine.instructions[insn.address] = engine._classify_instruction(insn)
    det = FunctionDetector(engine, engine.image, None, None)
    return det, section, BASE + target_offset

def test_immediate_inside_memory_operand_is_not_a_function():
    # test [esi+edx*4+0x1018],ebx; ret. Offset +1 also happens to decode to ret.
    det, sec, target = detector(bytes.fromhex('859c9618100000c3'), 11)
    assert det.engine.probes_as_returning_body(target)
    det._pass_imm_ref_targets([sec])
    assert target not in det._candidates

def test_real_prologue_can_realign_an_overlapping_sweep():
    # Sweep sees mov eax,0xc3ec8b55; the referenced entry is push ebp;mov ebp,esp;ret.
    det, sec, target = detector(bytes.fromhex('b8558becc3c3'), 11)
    assert det.engine.instruction_covering(target) is not None
    det._pass_imm_ref_targets([sec])
    assert target in det._candidates

def test_instruction_boundary_remains_a_candidate():
    det, sec, target = detector(bytes.fromhex('558becc3'), 10)
    det._pass_imm_ref_targets([sec])
    assert target in det._candidates

def test_immediate_naming_the_jump_after_a_cmp_is_not_a_function():
    # cmp eax,1; then jne +1; inc eax; ret. The constant names the jne, which
    # would branch on flags no caller hands it (JSRF: an IOCTL code, 0x74004,
    # cut the result banner's update before the jne its cmp feeds).
    det, sec, target = detector(bytes.fromhex('83f801750140c3'), 13)
    det._pass_imm_ref_targets([sec])
    assert target not in det._candidates

def test_entry_that_sets_flags_before_branching_remains_a_candidate():
    # test eax,eax; jne +1; inc eax; ret
    det, sec, target = detector(bytes.fromhex('85c0750140c3'), 10)
    det._pass_imm_ref_targets([sec])
    assert target in det._candidates

def test_immediate_naming_code_that_runs_into_it_is_not_a_function():
    # add eax,4; then push eax; pop eax; ret. The constant names the push,
    # which the add runs into (JSRF: 198,000, 0x00030570, cut the mission
    # file loader after its first state's `add eax, 4`).
    det, sec, target = detector(bytes.fromhex('83c004' '5058c3'), 13)
    det._pass_imm_ref_targets([sec])
    assert target not in det._candidates

def test_entry_after_a_call_that_never_returns_remains_a_candidate():
    # call <a routine that never returns>; then push ebp; mov ebp,esp; ret.
    # Control ends at the call, as at the end of JSRF's loader threads.
    call = b'\xe8' + struct.pack('<i', 5 - 15)    # at +10, to the ret at +5
    det, sec, target = detector(call + bytes.fromhex('558becc3'), 15)
    det.engine.no_return.add(BASE + 5)
    det._pass_imm_ref_targets([sec])
    assert target in det._candidates

def test_immediate_naming_a_switch_tables_last_bytes_is_not_a_function():
    # jmp [eax*4+table]; three arms; then ret; ret. The constant names the
    # table's last byte, 00, which decodes as add bl,al; ret (JSRF:
    # DirectSound's 0x0003010F, in sub_0002F7F0's table).
    table = BASE + 17
    arms = b''.join(struct.pack('<I', BASE + 5) for _ in range(3))
    tail = b'\xff\x24\x85' + struct.pack('<I', table) + arms + b'\xc3\xc3'
    det, sec, target = detector(tail, 28)
    det.engine.resync_jump_tables()
    assert det.engine.jump_tables.get(table) == table + 12
    det._pass_imm_ref_targets([sec])
    assert target not in det._candidates

def test_constant_return_stub_can_realign_an_overlapping_sweep():
    det, sec, target = detector(bytes.fromhex('b9b878563412c3'), 11)
    assert det.engine.instruction_covering(target) is not None
    det._pass_imm_ref_targets([sec])
    assert target in det._candidates

def test_virtual_dispatch_thunk_can_realign_an_overlapping_sweep():
    det, sec, target = detector(bytes.fromhex('b98b01ff6010c3'), 11)
    assert det.engine.instruction_covering(target) is not None
    det._pass_imm_ref_targets([sec])
    assert target in det._candidates


def test_immediate_direct_thunk_requires_a_returning_destination():
    # The address-taken entry jumps backward to a separate returning body.
    for destination, recovered in [(b"\xc3", True), (b"\xcc", False)]:
        det, sec, target = detector(
            destination + b"\x90" * 5 + bytes.fromhex("e9f5ffffff"), 16)
        det._pass_imm_ref_targets([sec])
        assert (target in det._candidates) is recovered
