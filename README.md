# FinalWriter Text DataType for AmigaOS

A BOOPSI DataType subclass of `text.datatype` that reads FinalWriter document files (`.fw`) and renders them through the AmigaOS DataTypes system.

## Files

| File | Description |
|------|-------------|
| `FinalWriter.dt` | DataType descriptor for `DEVS:DataTypes/` |
| `finalwriter.datatype` | Class library for `Classes:DataTypes/` |
| `finalwriter_datatype.c` | C source code |
| `gendt.py` | Python script to generate the `.dt` descriptor |
| `Makefile` / `SMakefile` / `Makefile.gcc` | Build files |

## Installation

1. Copy `FinalWriter.dt` to `DEVS:DataTypes/`
2. Copy `finalwriter.datatype` to `Classes:DataTypes/`
3. Run: `AddDataTypes REFRESH`

Now MultiView and other DataType-aware applications can open FinalWriter documents.

## Building

### On Amiga (SAS/C):
```
make
```

### Cross-compilation (GCC):
```
make -f Makefile.gcc
```

### Generate descriptor only:
```
python gendt.py > FinalWriter.dt
```

### Checking the parser:
```
make check
```
This builds `swrt_datatype/swrt_test.c` with the host compiler and runs it
over the FinalWriter document `testdata/SuperMacrosDOCS.FinalWriter`, dumping
the extracted text to `swrt_extracted.txt`. Use another document with:
```
make check TESTDOC=path/to/file.FinalWriter
```

## File Format Support

The DataType parses the FinalWriter IFF-based format:

- **SWRT** - top-level container
- **FDTA** - font definitions
- **ATTR+CHRS** - styled text runs (character attributes + text)
- **RULE** - paragraph breaks

### Supported Formatting

- Bold, Italic, Underline
- Superscript
- Font size
- Word wrap
- Paragraph layout

## Requirements

- AmigaOS 3.0 or later (DataTypes library v39+)
- SAS/C or GCC for rebuilding

## License

GNU General Public License v3.0 or later
