"""Read-only model of the guest ELF32 ARM shared object (vaddr space, symbols, relocations, PLT)."""
import bisect
import struct

from elftools.elf.elffile import ELFFile

R_ARM_RELATIVE = 23
R_ARM_JUMP_SLOT = 22
PLT0_SIZE = 20
PLT_STUB_SIZE = 12


def _prel31(word, place):
    off = word & 0x7FFFFFFF
    if off & 0x40000000:
        off -= 0x80000000
    return (place + off) & 0xFFFFFFFF


class GuestElf:
    """Attributes: base, text (lo, hi), exported {name: vaddr}, exidx_starts, reloc_ptrs, plt {stub: name},
    relocated (set of vaddrs that receive R_ARM_RELATIVE), imports_noreturn (stub vaddrs)."""

    def __init__(self):
        self.base = 0
        self.text = (0, 0)
        self.exported = {}
        self.exidx_starts = set()
        self.reloc_ptrs = set()
        self.relocated = set()
        self.plt = {}
        self._segments = []  # (vaddr, bytes)
        self._sym_addrs = []
        self._sym_names = []

    @classmethod
    def load(cls, path, base):
        self = cls()
        self.base = base
        with open(path, "rb") as fh:
            elf = ELFFile(fh)
            for seg in elf.iter_segments():
                if seg["p_type"] == "PT_LOAD":
                    data = seg.data() + b"\0" * (seg["p_memsz"] - seg["p_filesz"])
                    self._segments.append((seg["p_vaddr"], data))
            text = elf.get_section_by_name(".text")
            self.text = (text["sh_addr"], text["sh_addr"] + text["sh_size"])
            dynsym = elf.get_section_by_name(".dynsym")
            syms = []
            for s in dynsym.iter_symbols():
                if s["st_shndx"] == "SHN_UNDEF" or not s.name:
                    continue
                if s["st_info"]["type"] == "STT_FUNC":
                    self.exported[s.name] = s["st_value"]
                if s["st_info"]["type"] in ("STT_FUNC", "STT_OBJECT", "STT_NOTYPE"):
                    syms.append((s["st_value"] & ~1, s.name))
            syms.sort()
            self._sym_addrs = [a for a, _ in syms]
            self._sym_names = [n for _, n in syms]
            exidx = elf.get_section_by_name(".ARM.exidx")
            if exidx is not None:
                d, a = exidx.data(), exidx["sh_addr"]
                for i in range(0, len(d), 8):
                    self.exidx_starts.add(_prel31(struct.unpack_from("<I", d, i)[0], a + i) & ~1)
            for sec in elf.iter_sections():
                if not sec.name.startswith(".rel") or not hasattr(sec, "iter_relocations"):
                    continue
                for i, r in enumerate(sec.iter_relocations()):
                    if r["r_info_type"] == R_ARM_RELATIVE:
                        self.relocated.add(r["r_offset"])
                        v = self.read32(r["r_offset"])
                        if self.text[0] <= (v & ~1) < self.text[1]:
                            self.reloc_ptrs.add(v)
            plt = elf.get_section_by_name(".plt")
            relplt = elf.get_section_by_name(".rel.plt")
            if plt is not None and relplt is not None:
                for i, r in enumerate(relplt.iter_relocations()):
                    if r["r_info_type"] == R_ARM_JUMP_SLOT:
                        name = dynsym.get_symbol(r["r_info_sym"]).name
                        self.plt[plt["sh_addr"] + PLT0_SIZE + PLT_STUB_SIZE * i] = name
        return self

    def read_bytes(self, vaddr, n):
        for base, data in self._segments:
            if base <= vaddr and vaddr + n <= base + len(data):
                return data[vaddr - base:vaddr - base + n]
        raise ValueError(f"vaddr {vaddr:#x} not in a loaded segment")

    def read32(self, vaddr):
        return struct.unpack("<I", self.read_bytes(vaddr, 4))[0]

    def nearest_symbol(self, vaddr):
        i = bisect.bisect_right(self._sym_addrs, vaddr) - 1
        if i < 0:
            return None, 0
        return self._sym_names[i], vaddr - self._sym_addrs[i]


class BytesImage:
    """Minimal stand-in for GuestElf over a raw code buffer (tests)."""

    def __init__(self, code, base_vaddr):
        self.base = 0
        self.text = (base_vaddr, base_vaddr + len(code))
        self.exported = {}
        self.exidx_starts = set()
        self.reloc_ptrs = set()
        self.relocated = set()
        self.plt = {}
        self._code = code
        self._lo = base_vaddr

    def read_bytes(self, vaddr, n):
        off = vaddr - self._lo
        if off < 0 or off + n > len(self._code):
            raise ValueError(f"vaddr {vaddr:#x} outside code")
        return self._code[off:off + n]

    def read32(self, vaddr):
        return struct.unpack("<I", self.read_bytes(vaddr, 4))[0]

    def nearest_symbol(self, vaddr):
        return None, 0
