import struct

from recomp.discover import discover_bytes


def test_plt_names(guest_elf):
    assert guest_elf.plt[0x150D08] == "__cxa_atexit"
    assert len(guest_elf.plt) == 208


def test_elf_basics(guest_elf):
    lo, hi = guest_elf.text
    assert lo == 0x1516C8 and hi == 0x3F2D60
    assert guest_elf.exported["Java_com_wbgames_LEGOgame_GameGLSurfaceView_nativeRender"] == 0x17331C
    assert guest_elf.read32(0x17331C) == struct.unpack_from("<I", guest_elf.read_bytes(0x17331C, 4))[0]
    assert len(guest_elf.exidx_starts) > 10000
    assert len(guest_elf.reloc_ptrs) > 5000


def test_counts_real(program):
    assert len(program.functions) >= 14000
    unresolved = [i for i in program.issues if "unresolved" in i]
    assert not unresolved, unresolved[:10]


def test_switch_tables_real(program):
    tables = [(f, jt) for f in program.functions.values() for jt in f.jump_tables.values() if jt.kind == "add"]
    sites = {a for f in program.functions.values() for a, i in f.insns.items()
             if i.mnemonic.startswith("add") and i.op_str.startswith("pc, pc,") and "lsl #2" in i.op_str}
    assert {jt.at for _, jt in tables} == sites  # every reachable switch is recovered
    assert len(tables) == 291  # the census's 300 add-pc words include 9 unreachable non-switch words
    for f, jt in tables:
        assert jt.count >= 1
        for t in jt.targets:
            assert t in f.insns, (hex(f.entry), hex(t))


def test_thumb_found(program, guest_elf):
    names = set(program.thumb_functions.values())
    assert "_ZNSt12__node_alloc11_M_allocateERj" in names
    assert "__cxa_type_match" in names
    assert guest_elf.exported["_ZNSt12__node_alloc11_M_allocateERj"] & ~1 in program.thumb_functions
    # ARM game functions that start with conditional instructions must stay ARM
    assert guest_elf.exported["_Z21SaveGame_GetLevelDataP12SAVEGAMEDATA5LEVEL17SAVEGAMELEVELDATAb"] in program.functions
    assert len(program.thumb_functions) < 200
    assert all(e not in program.functions for e in program.thumb_functions)


def _arm(*words):
    return b"".join(struct.pack("<I", w) for w in words)


def test_synthetic_switch():
    # 0: cmp r0,#2 ; 4: addls pc,pc,r0,lsl #2 ; 8: b dflt ; 12: b c0 ; 16: b c1 ; 20: b c2 ;
    # 24: c0: bx lr ; 28: c1: bx lr ; 32: c2: bx lr ; 36: dflt: bx lr
    code = _arm(0xE3500002, 0x908FF100, 0xEA000005, 0xEA000001, 0xEA000001, 0xEA000001,
                0xE12FFF1E, 0xE12FFF1E, 0xE12FFF1E, 0xE12FFF1E)
    prog = discover_bytes(code, base_vaddr=0x1000, entries=[0x1000])
    f = prog.functions[0x1000]
    assert len(f.jump_tables) == 1
    jt = next(iter(f.jump_tables.values()))
    assert jt.count == 3 and jt.index_reg == "r0"
    assert jt.targets == [0x100C, 0x1010, 0x1014]
    assert jt.default == 0x1008
    assert not prog.issues


def test_synthetic_tail_call_and_call():
    # f1: bl f2 ; b f2   (tail call)       f2: bx lr
    code = _arm(0xEB000000, 0xEAFFFFFF, 0xE12FFF1E)
    prog = discover_bytes(code, base_vaddr=0x2000, entries=[0x2000, 0x2008])
    f1 = prog.functions[0x2000]
    assert 0x2008 in f1.callees
    assert 0x2008 in f1.tail_targets
    assert 0x2008 not in f1.insns


def test_literal_pool_not_code():
    # ldr r0,[pc,#0] ; bx lr ; .word 0xFFFFFFFF (pool, would be an undefined instruction)
    code = _arm(0xE59F0000, 0xE12FFF1E, 0xFFFFFFFF)
    prog = discover_bytes(code, base_vaddr=0x3000, entries=[0x3000])
    assert 0x3008 not in prog.functions[0x3000].insns
    assert not prog.issues


def test_symbol_parity_decides_mode(program, guest_elf):
    """ELF marks Thumb functions with an odd symbol value; even named entries are ARM even inside the Thumb
    runtime block (libgcc's 64-bit division helpers are ARM there)."""
    lo, hi = guest_elf.text
    for name, v in guest_elf.exported.items():
        if not lo <= (v & ~1) < hi:
            continue
        if v & 1:
            assert (v & ~1) in program.thumb_functions, name
        elif (v & ~1) not in program.thumb_functions:
            continue
        else:
            assert False, f"{name} at {v:#x} has an even (ARM) symbol but was classified Thumb"
    assert guest_elf.exported["__aeabi_ldivmod"] in program.functions
