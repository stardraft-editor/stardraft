"""Checks that a .pdb is the one of an .exe: the exe's debug entry (CodeView RSDS) names the PDB's GUID and age, as a
debugger (Visual Studio, WinDbg) checks them before it opens a crash.dmp with the symbols. Needs only Python.

usage: python -I tools/ci/pdb_check.py <stardraft.exe> <stardraft.pdb>
"""

import struct
import sys
import uuid

MSF_MAGIC = b"Microsoft C/C++ MSF 7.00\r\n\x1aDS\0\0\0"


def exe_codeview(data):
    """(GUID, age, pdb path) of the exe's CodeView debug entry."""
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    if data[pe:pe + 4] != b"PE\0\0":
        raise ValueError("not a PE file")
    sections = struct.unpack_from("<H", data, pe + 6)[0]
    opt_size = struct.unpack_from("<H", data, pe + 20)[0]
    opt = pe + 24
    magic = struct.unpack_from("<H", data, opt)[0]
    dirs = opt + (112 if magic == 0x20B else 96)  # (PE32+ or PE32: the data directories)
    debug_rva, debug_size = struct.unpack_from("<II", data, dirs + 6 * 8)
    sect = opt + opt_size

    def offset(rva):
        for i in range(sections):
            vsize, va, rawsize, raw = struct.unpack_from("<IIII", data, sect + i * 40 + 8)
            if va <= rva < va + max(vsize, rawsize):
                return raw + rva - va
        raise ValueError(f"no section has RVA {rva:#x}")

    pos = offset(debug_rva)
    for i in range(debug_size // 28):
        kind, size, _, raw = struct.unpack_from("<IIII", data, pos + i * 28 + 12)
        if kind == 2 and data[raw:raw + 4] == b"RSDS":  # IMAGE_DEBUG_TYPE_CODEVIEW
            guid = uuid.UUID(bytes_le=bytes(data[raw + 4:raw + 20]))
            age = struct.unpack_from("<I", data, raw + 20)[0]
            path = bytes(data[raw + 24:raw + size]).split(b"\0")[0].decode("utf-8", "replace")
            return guid, age, path
    raise ValueError("the exe has no CodeView debug entry (linked without /DEBUG?)")


def pdb_streams(data):
    if not data.startswith(MSF_MAGIC):
        raise ValueError("not an MSF 7.00 PDB")
    block, _, _, dir_bytes, _, map_addr = struct.unpack_from("<IIIIII", data, len(MSF_MAGIC))
    blocks = lambda n: (n + block - 1) // block
    dir_blocks = struct.unpack_from(f"<{blocks(dir_bytes)}I", data, map_addr * block)
    directory = b"".join(data[b * block:(b + 1) * block] for b in dir_blocks)[:dir_bytes]
    count = struct.unpack_from("<I", directory, 0)[0]
    sizes = struct.unpack_from(f"<{count}I", directory, 4)
    pos, streams = 4 + 4 * count, []
    for s in sizes:
        n = 0 if s == 0xFFFFFFFF else blocks(s)
        ids = struct.unpack_from(f"<{n}I", directory, pos)
        pos += 4 * n
        streams.append(b"".join(data[b * block:(b + 1) * block] for b in ids)[:s if n else 0])
    return streams


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    exe, pdb = (open(p, "rb").read() for p in sys.argv[1:])
    guid, age, path = exe_codeview(exe)
    streams = pdb_streams(pdb)
    _, _, info_age = struct.unpack_from("<III", streams[1], 0)  # the PDB stream: version, signature, age, GUID
    info_guid = uuid.UUID(bytes_le=bytes(streams[1][12:28]))
    dbi_age = struct.unpack_from("<I", streams[3], 8)[0] if len(streams) > 3 and len(streams[3]) >= 12 else None
    print(f"exe: GUID {guid}, age {age}, {path}")
    print(f"pdb: GUID {info_guid}, age {info_age} (DBI age {dbi_age})")
    if info_guid != guid or age not in (info_age, dbi_age):
        print("the pdb is not the one of this exe")
        return 1
    print("the pdb matches the exe")
    return 0


if __name__ == "__main__":
    sys.exit(main())
