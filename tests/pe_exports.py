"""Inspect PE exports/imports without loading code or requiring dumpbin."""
from pathlib import Path
import struct
import sys

blob = Path(sys.argv[1]).read_bytes()
def u16(offset): return struct.unpack_from('<H', blob, offset)[0]
def u32(offset): return struct.unpack_from('<I', blob, offset)[0]
pe = u32(0x3c)
assert blob[:2] == b'MZ' and blob[pe:pe+4] == b'PE\0\0'
count = u16(pe + 6)
optional = pe + 24
directory = optional + (112 if u16(optional) == 0x20b else 96)
sections = optional + u16(pe + 20)
def offset(rva):
    for i in range(count):
        section = sections + 40 * i
        size, address, raw_size, raw_offset = struct.unpack_from('<IIII', blob, section + 8)
        if address <= rva < address + max(size, raw_size):
            return raw_offset + rva - address
    raise ValueError(f'Unmapped RVA {rva:x}')
def string(at): return blob[at:blob.index(b'\0', at)].decode('ascii')
exports = offset(u32(directory))
name_count = u32(exports + 24)
names = offset(u32(exports + 32))
actual = sorted(string(offset(u32(names + 4 * i))) for i in range(name_count))
assert actual == ['KISA_Attempt', 'KISA_Compare', 'KISA_Enroll'], actual
imports = []
if u32(directory + 8):
    entry = offset(u32(directory + 8))
    while u32(entry + 12):
        imports.append(string(offset(u32(entry + 12))).upper())
        entry += 20
assert set(imports) <= {'KERNEL32.DLL'}, imports
print('Exactly three undecorated KISA exports; thin DLL imports only KERNEL32:', imports)
