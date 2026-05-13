#!/usr/bin/env python3
"""Generate FinalWriter DataType descriptor (.dt) file.

Usage: python gendt.py > FinalWriter.dt
"""

import struct
import sys

def chunks(name, data):
    buf = struct.pack('>4sI', name.encode(), len(data)) + data
    if len(buf) & 1:
        buf += b'\0'
    return buf

def make_id(s):
    return struct.unpack('>I', s.encode())[0]

pad = b'\0\0'

name = 'FinalWriter Document'
base = 'text'
pattern = '#?.fw'
mask = None
mask_data = None
mask_len = 0

dth_name = name.encode('latin-1') + b'\0'
dth_basename = base.encode('latin-1') + b'\0'
dth_pattern = pattern.encode('latin-1') + b'\0'
dth_mask = struct.pack('>8s', b'\xff' * 8)
dth_group_id = 0x40000000  # GID_TEXT
dth_id = make_id('FWR1')
dth_flags = 1  # DTF_ASCII
dth_priority = 0

tdhd_data = (
    struct.pack('>I', 0) +  # offset to dth_Name (patched below)
    struct.pack('>I', 0) +  # offset to dth_BaseName
    struct.pack('>I', 0) +  # offset to dth_Pattern
    struct.pack('>I', 0) +  # offset to dth_Mask
    struct.pack('>I', dth_group_id) +
    struct.pack('>I', dth_id) +
    struct.pack('>h', mask_len) +
    struct.pack('>h', 0) +
    struct.pack('>H', dth_flags) +
    struct.pack('>h', dth_priority)
)

name_off = 44
base_off = name_off + len(dth_name)
pat_off = base_off + len(dth_basename)
mask_off = pat_off + len(dth_pattern)

names_data = dth_name + dth_basename + dth_pattern

tdhd = struct.pack('>I', name_off)
tdhd += struct.pack('>I', base_off)
tdhd += struct.pack('>I', pat_off)
tdhd += struct.pack('>I', mask_off if mask else 0)
tdhd += struct.pack('>I', dth_group_id)
tdhd += struct.pack('>I', dth_id)
tdhd += struct.pack('>h', mask_len)
tdhd += struct.pack('>h', 0)
tdhd += struct.pack('>H', dth_flags)
tdhd += struct.pack('>h', dth_priority)

tdme_data = b'\xff' * 8

data = chunks('TDHD', tdhd)
data += chunks('TDPT', dth_pattern)
data += chunks('TDNA', dth_name)
data += chunks('TDBN', dth_basename)
if mask:
    data += chunks('TDME', tdme_data)

form = struct.pack('>4sI', b'FORM', len(data) + 4) + b'FDTE' + data

sys.stdout.buffer.write(form)
