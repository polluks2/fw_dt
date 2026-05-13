#!/usr/bin/env python3
"""Generate FinalWriter DataType descriptor (.dt) file.

Usage: python gendt.py > FinalWriter.dt
"""

import struct
import sys

def pad(b):
    if len(b) & 1:
        return b + b'\0'
    return b

# build the DFILE chunks in order; calculate TDHD offsets from actual positions

pat = b'#?.fw\0'
nam = b'FinalWriter Document\0'
bas = b'text\0'
mdata = struct.pack('>HHHH', 0x53FF, 0x57FF, 0x52FF, 0x54FF)  # S W R T mask words
mask_len = 4

# Layout inside FORM FDTE:
#   TDHD(8+32)  TDPT(8+6)  TDNA(8+21+pad)  TDBN(8+5+pad)  TDME(8+8)

off = 0
tdhd_chunk_start = off
off += 4  # chunk ID
off += 4  # chunk size
tdhd_data_start = off  # absolute offset of DataTypeHeader struct
off += 32  # DataTypeHeader is 32 bytes

pat_data_start = off
pat_full = pad(b'TDPT' + struct.pack('>I', len(pat)) + pat)
off += len(pat_full)

nam_data_start = off
nam_full = pad(b'TDNA' + struct.pack('>I', len(nam)) + nam)
off += len(nam_full)

bas_data_start = off
bas_full = pad(b'TDBN' + struct.pack('>I', len(bas)) + bas)
off += len(bas_full)

mdata_data_start = off
mdata_full = pad(b'TDME' + struct.pack('>I', len(mdata)) + mdata)

# Offsets relative to TDHD data start
name_off = nam_data_start - tdhd_data_start
base_off = bas_data_start - tdhd_data_start
pat_off  = pat_data_start - tdhd_data_start
mask_off = mdata_data_start - tdhd_data_start

tdhd_body = struct.pack('>IIII', name_off, base_off, pat_off, mask_off)
tdhd_body += struct.pack('>II', 0x40000000, struct.unpack('>I', b'FWR1')[0])
tdhd_body += struct.pack('>hh', mask_len, 0)
tdhd_body += struct.pack('>Hh', 1, 0)  # DTF_ASCII, priority=0

# Assemble FORM
form_data = (b'FDTE'
    + struct.pack('>4sI', b'TDHD', len(tdhd_body)) + tdhd_body
    + pat_full + nam_full + bas_full + mdata_full)

buffer = b'FORM' + struct.pack('>I', len(form_data)) + form_data

sys.stdout.buffer.write(buffer)
