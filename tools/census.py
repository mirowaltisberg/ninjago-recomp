#!/usr/bin/env python3
"""Instruction census of the guest library (libLEGO_M1.so, ARMv7 ARM mode).

Reports function-start sources, mnemonic frequencies, control-flow patterns
that the recompiler must handle, and VFP/NEON usage.

    tools/.venv/bin/python -I tools/census.py input/extracted/lib/armeabi-v7a/libLEGO_M1.so
"""
import collections
import struct
import sys

import capstone
from capstone import arm
from elftools.elf.elffile import ELFFile


def prel31(word, place):
    off = word & 0x7FFFFFFF
    if off & 0x40000000:
        off -= 0x80000000
    return (place + off) & 0xFFFFFFFF


def main(path):
    elf = ELFFile(open(path, "rb"))
    text = elf.get_section_by_name(".text")
    t_addr, t_data = text["sh_addr"], text.data()
    t_end = t_addr + len(t_data)

    # Function starts: exported symbols, EHABI index table, pointers in data (relocations)
    dyn = {s["st_value"] & ~1 for s in elf.get_section_by_name(".dynsym").iter_symbols()
           if s["st_info"]["type"] == "STT_FUNC" and s["st_shndx"] != "SHN_UNDEF"}
    exidx_sec = elf.get_section_by_name(".ARM.exidx")
    ex_data, ex_addr = exidx_sec.data(), exidx_sec["sh_addr"]
    exidx = {prel31(struct.unpack_from("<I", ex_data, i)[0], ex_addr + i) for i in range(0, len(ex_data), 8)}
    reloc_ptrs = set()
    image = {}
    for seg in elf.iter_segments():
        if seg["p_type"] == "PT_LOAD":
            image[seg["p_vaddr"]] = seg.data()

    def read32(addr):
        for base, data in image.items():
            if base <= addr < base + len(data) - 3:
                return struct.unpack_from("<I", data, addr - base)[0]
        return None

    for sec in elf.iter_sections():
        if sec.name.startswith(".rel") and hasattr(sec, "iter_relocations"):
            for r in sec.iter_relocations():
                if r["r_info_type"] == 23:  # R_ARM_RELATIVE
                    v = read32(r["r_offset"])
                    if v is not None and t_addr <= (v & ~1) < t_end:
                        reloc_ptrs.add(v & ~1)

    md = capstone.Cs(capstone.CS_ARCH_ARM, capstone.CS_MODE_ARM)
    md.detail = True

    # Pass 1: literal-pool words (targets of ldr Rx, [pc, #imm]) are data, not code.
    # skipdata: capstone otherwise stops at the first undecodable word.
    pool = set()
    md.skipdata = True
    for ins in md.disasm(t_data, t_addr):
        if ins.id in (arm.ARM_INS_LDR, arm.ARM_INS_VLDR, arm.ARM_INS_LDRD) and len(ins.operands) >= 2:
            op = ins.operands[-1]
            if op.type == arm.ARM_OP_MEM and op.mem.base == arm.ARM_REG_PC and op.mem.index == 0:
                tgt = ins.address + 8 + op.mem.disp
                pool.add(tgt)
                if ins.id != arm.ARM_INS_LDR:
                    pool.add(tgt + 4)
    md.skipdata = False

    mnem = collections.Counter()
    groups = collections.Counter()
    patterns = collections.Counter()
    bl_targets = set()
    undecodable = 0
    addr = t_addr
    while addr < t_end:
        if addr in pool:
            addr += 4
            continue
        chunk = t_data[addr - t_addr: addr - t_addr + 4]
        ins = next(md.disasm(chunk, addr), None)
        if ins is None:
            undecodable += 1
            addr += 4
            continue
        mnem[ins.mnemonic.split(".")[0].rstrip("seqnlhigtcvmpa") or ins.mnemonic] += 0  # keep key space small
        base = capstone.Cs.insn_name(md, ins.id)
        mnem[base] += 1
        if ins.cc not in (arm.ARM_CC_AL, arm.ARM_CC_INVALID):
            patterns["conditional (non-AL)"] += 1
        regs_w = ins.regs_access()[1]
        if arm.ARM_REG_PC in regs_w and ins.id not in (arm.ARM_INS_B, arm.ARM_INS_BL):
            kind = {arm.ARM_INS_LDR: "ldr pc, [...] (jump table / return)",
                    arm.ARM_INS_POP: "pop {..pc} (return)",
                    arm.ARM_INS_LDM: "ldm {..pc} (return)",
                    arm.ARM_INS_ADD: "add pc, pc, rX (jump table)",
                    arm.ARM_INS_MOV: "mov pc, rX",
                    arm.ARM_INS_BX: "bx rX",
                    arm.ARM_INS_BLX: "blx rX (indirect call)"}.get(ins.id, f"other pc write: {base}")
            if ins.id == arm.ARM_INS_BX and ins.operands[0].reg == arm.ARM_REG_LR:
                kind = "bx lr (return)"
            patterns[kind] += 1
        if ins.id == arm.ARM_INS_BL:
            bl_targets.add(ins.operands[0].imm)
        if ins.id == arm.ARM_INS_BLX and ins.operands[0].type == arm.ARM_OP_IMM:
            patterns["blx imm (ARM->Thumb switch!)"] += 1
        for g in ins.groups:
            gname = ins.group_name(g)
            if gname in ("neon", "vfp2", "vfp3", "vfp4", "fparmv8", "crypto", "v6", "v6t2", "v7", "trustzone"):
                groups[gname] += 1
        if ins.id in (arm.ARM_INS_LDREX, arm.ARM_INS_STREX, arm.ARM_INS_LDREXB, arm.ARM_INS_STREXB,
                      arm.ARM_INS_LDREXD, arm.ARM_INS_STREXD, arm.ARM_INS_DMB, arm.ARM_INS_MRC,
                      arm.ARM_INS_MCR, arm.ARM_INS_SVC):
            patterns[f"special: {base}"] += 1
        addr += 4

    starts = dyn | exidx | reloc_ptrs | {t & ~1 for t in bl_targets if t_addr <= t < t_end}
    print(f".text {t_addr:#x}-{t_end:#x} ({len(t_data)} bytes), literal-pool words: {len(pool)}, undecodable words: {undecodable}")
    print(f"function starts: exported={len(dyn)} exidx={len(exidx)} reloc-ptrs={len(reloc_ptrs)} bl-targets={len(bl_targets)} union={len(starts)}")
    print(f"  not exported but known: {len(starts - dyn)}")
    print("\ncontrol-flow / special patterns:")
    for k, v in patterns.most_common():
        print(f"  {v:8d}  {k}")
    print("\ninstruction groups:")
    for k, v in groups.most_common():
        print(f"  {v:8d}  {k}")
    total = sum(v for v in mnem.values())
    print(f"\nmnemonics: {len([k for k, v in mnem.items() if v])} distinct, {total} instructions")
    for k, v in mnem.most_common():
        if v:
            print(f"  {v:8d}  {k}")


if __name__ == "__main__":
    main(sys.argv[1])
