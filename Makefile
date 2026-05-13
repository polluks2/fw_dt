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

all: $(DATATYPE) $(DESCRIPTOR)

$(DATATYPE): $(OBJS)
	$(CC) $(LFLAGS) $(OBJS) to $(DATATYPE)

.obj.c:
	$(CC) $(CFLAGS) $<

$(DESCRIPTOR):
	python gendt.py > $(DESCRIPTOR)

clean:
	-delete $(OBJS) $(DATATYPE) $(DESCRIPTOR) QUIET

install: $(DATATYPE) $(DESCRIPTOR)
	copy $(DATATYPE) Classes:DataTypes/
	copy $(DESCRIPTOR) DEVS:DataTypes/
	echo "Run: AddDataTypes REFRESH"
