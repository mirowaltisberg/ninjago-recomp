import struct

import capstone

from recomp.discover import discover_bytes
from recomp.elfimage import BytesImage
from recomp.emit import emit_function
from recomp.lift import LiftCtx


def _arm(*words):
    return b"".join(struct.pack("<I", w) for w in words)


class _Elf(BytesImage):
    plt = {}


def test_entry_after_shared_code_starts_at_entry():
    # 0x1000: mov r0,#1 ; 0x1004: bx lr        (shared block, only reached by the branch below)
    # 0x1008: cmp r0,#0 ; beq 0x1000 ; mov r0,#2 ; bx lr   <- the entry
    code = _arm(0xE3A00001, 0xE12FFF1E, 0xE3500000, 0x0AFFFFFB, 0xE3A00002, 0xE12FFF1E)
    prog = discover_bytes(code, base_vaddr=0x1000, entries=[0x1008])
    fn = prog.functions[0x1008]
    assert 0x1000 in fn.insns  # the shared block belongs to the function
    md = capstone.Cs(capstone.CS_ARCH_ARM, capstone.CS_MODE_ARM)
    md.detail = True
    text, why = emit_function(fn, prog, _Elf(code, 0x1000), LiftCtx(base=0x100000), md, {})
    assert why is None
    body = text.split("{", 1)[1]
    first_stmt = next(line.strip() for line in body.splitlines() if line.strip() and "entry_lr" not in line)
    assert first_stmt == "goto L_00001008;"
    assert "L_00001008: ;" in text
