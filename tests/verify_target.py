#!/usr/bin/env python3
"""Read-only check of the supplied xdBot binary against the bridge profile.
Usage: python tests/verify_target.py /path/to/xdBot.geode [or zilko.xdbot.dll]
This validates static evidence. It does not run Geometry Dash or replace an in-game test.
"""
import hashlib
import json
from pathlib import Path
import struct
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[1]

def verify(data):
    profile = json.loads((ROOT / 'docs/binary-profile.json').read_text())
    assert hashlib.sha256(data).hexdigest() == profile['dll_sha256'], 'Unsupported DLL fingerprint'
    u16 = lambda p: struct.unpack_from('<H', data, p)[0]
    u32 = lambda p: struct.unpack_from('<I', data, p)[0]
    pe = u32(0x3c)
    assert data[:2] == b'MZ' and data[pe:pe+4] == b'PE\0\0'
    coff, opt = pe + 4, pe + 24
    assert u16(coff) == 0x8664 and u16(opt) == 0x20b
    assert len(data) == profile['size_bytes']
    assert u32(coff + 4) == profile['pe_timestamp']
    assert u32(opt + 56) == profile['image_size']
    sections = []
    for i in range(u16(coff + 2)):
        p = opt + u16(coff + 16) + 40 * i
        virtual_size, rva, size, offset = struct.unpack_from('<IIII', data, p + 8)
        sections.append((rva, size, offset))
    def offset(rva):
        for start, size, file in sections:
            if start <= rva < start + size:
                return file + rva - start
        raise AssertionError(f'RVA outside file: {rva:#x}')
    def at(rva, size):
        p = offset(rva)
        return data[p:p+size]
    def string(rva):
        p = offset(rva)
        return data[p:data.index(b'\0', p)].decode()
    for name, entry in profile['entries'].items():
        expected = bytes.fromhex(entry['prefix'])
        assert at(entry['rva'], len(expected)) == expected, name

    # Confirm exact function boundaries from Windows unwind metadata.
    pdata, pdata_size = struct.unpack_from('<II', data, opt + 112 + 8 * 3)
    functions = {}
    for i in range(pdata_size // 12):
        start, end, _ = struct.unpack('<III', at(pdata + i * 12, 12))
        functions[start] = end
    for entry in profile['entries'].values():
        assert entry['rva'] in functions
    load = profile['entries']['macro_cell_load']['rva']
    call = profile['toggle_playing_call_rva']
    assert load < call < functions[load]
    assert at(call, 1) == b'\xe8'
    target = call + 5 + struct.unpack('<i', at(call + 1, 4))[0]
    assert target == profile['entries']['toggle_playing']['rva']
    assert string(profile['macro_loaded_string_rva']) == 'Macro Loaded'
    assert at(profile['macro_loaded_call_rva'], 2) == b'\xff\xd0'

    # Validate the two IAT slots used for typed Geode hooks, not guessed exports.
    imports = {}
    imp, _ = struct.unpack_from('<II', data, opt + 112 + 8)
    p = offset(imp)
    while True:
        original, stamp, chain, name, first = struct.unpack_from('<IIIII', data, p)
        if not any((original, stamp, chain, name, first)): break
        i = 0
        while True:
            thunk = struct.unpack('<Q', at((original or first) + 8*i, 8))[0]
            if not thunk: break
            if not thunk & (1 << 63): imports[first + 8*i] = string(thunk + 2)
            i += 1
        p += 20
    assert imports[0x2af130].startswith('?create@Notification@geode@@')
    assert 'W4NotificationIcon' in imports[0x2af130]
    assert imports[0x2af4f0] == '?show@Notification@geode@@QEAAXXZ'
    print('PASS binary: SHA-256, PE identity, four function boundaries/prefixes, native autoplay call, success notification, Geode imports')

if __name__ == '__main__':
    path = Path(sys.argv[1])
    if zipfile.is_zipfile(path):
        with zipfile.ZipFile(path) as archive: data = archive.read('zilko.xdbot.dll')
    else: data = path.read_bytes()
    verify(data)
    changed = bytearray(data); changed[-1] ^= 1
    try:
        verify(changed)
    except AssertionError as error:
        assert str(error) == 'Unsupported DLL fingerprint'
        print('PASS unsupported/mutated DLL rejected before using addresses')
    else:
        raise AssertionError('A mismatched DLL was accepted')
