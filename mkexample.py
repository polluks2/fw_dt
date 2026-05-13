#!/usr/bin/env python3
"""Generate example FinalWriter document for testing the DataType."""

import struct

def pad(b):
    return b + b'\0' if len(b) & 1 else b

def chunk(tag, data):
    return struct.pack('>4sI', tag.encode(), len(data)) + data

def attr_run(text, style=0, flags=0, fontsize=12):
    """Create an ATTR+CHRS pair for a run of styled text."""
    cdata = text.encode('latin-1')
    ab = bytearray(22)
    ab[0:4]  = struct.pack('>I', len(cdata))    # bytes 0-3: charCount
    ab[4:6]  = struct.pack('>H', 0)              # bytes 4-5: fontIndex (0 = default)
    ab[6]    = 0                                 # byte 6: ?
    ab[7]    = fontsize                          # byte 7: fontSize
    ab[8]    = 0                                 # byte 8: ?
    ab[9]    = style                             # byte 9: emphasis
    ab[10]   = 0                                 # byte 10: ?
    ab[11]   = flags                             # byte 11: super/sub
    return chunk('ATTR', bytes(ab)) + chunk('CHRS', pad(cdata))

def paragraph():
    return chunk('RULE', b'\0' * 24)

def fw_doc(filename, runs):
    buf = b'SWRT'
    for r in runs:
        buf += r
    with open(filename, 'wb') as f:
        f.write(buf)

doc = [
    attr_run("FinalWriter DataType Demo\n\n"),
    attr_run("This document demonstrates the FinalWriter text DataType.\n\n"),
    attr_run("Bold text", style=4),
    attr_run("  "),
    attr_run("Italic text", style=8),
    attr_run("  "),
    attr_run("Underlined text", style=1),
    attr_run("\n"),
    attr_run("Bold+Italic", style=12),
    attr_run("\n\n"),
    attr_run("Superscript: E=mc"),
    attr_run("2", flags=1),
    attr_run("\n"),
    attr_run("Subscript: H"),
    attr_run("2", flags=1),
    attr_run("O\n\n"),
    attr_run("Paragraph two.  MultiView can display this file\n"),
    attr_run("thanks to the FinalWriter DataType.\n"),
    paragraph(),
]

fw_doc('example.fw', doc)
print("Created example.fw")
