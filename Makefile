#
# Makefile for FinalWriter Text DataType
#
# Usage on Amiga with SAS/C:  make
# Usage with GCC cross:       make CC=m68k-amigaos-gcc
#

DATATYPE = finalwriter.datatype
DESCRIPTOR = FinalWriter.dt
CC = sc
CFLAGS = -c -dNODEBUG -dAMIGA
LFLAGS = link lib cnlist

OBJS = finalwriter_datatype.obj

# Native (host) check: parse a real FinalWriter document with the standalone
# SWRT parser.  Uses the host compiler, not the Amiga/cross toolchain.
HOSTCC ?= gcc
HOSTCFLAGS ?= -O2 -Wall -ansi -pedantic
TEST = swrt_datatype/swrt_test
TESTDOC ?= testdata/SuperMacrosDOCS.FinalWriter

all: $(DATATYPE) $(DESCRIPTOR)

$(DATATYPE): $(OBJS)
	$(CC) $(LFLAGS) $(OBJS) to $(DATATYPE)

.obj.c:
	$(CC) $(CFLAGS) $<

$(DESCRIPTOR):
	python gendt.py > $(DESCRIPTOR)

check: $(TEST)
	$(TEST) $(TESTDOC)

$(TEST): swrt_datatype/swrt_test.c
	$(HOSTCC) $(HOSTCFLAGS) -o $@ $<

clean:
	-delete $(OBJS) $(DATATYPE) $(DESCRIPTOR) QUIET
	-rm -f $(TEST) swrt_extracted.txt

install: $(DATATYPE) $(DESCRIPTOR)
	copy $(DATATYPE) Classes:DataTypes/
	copy $(DESCRIPTOR) DEVS:DataTypes/
	echo "Run: AddDataTypes REFRESH"
