CC    = gcc
CFLAGS = -O2 -Wall -ansi -pedantic -Wno-pointer-sign
AROS  = /usr/local/arossdk

INCLUDES = -I$(AROS)/include -I$(AROS)/include/aros
LDFLAGS  = -nostartfiles -shared -Wl,-soname,swrt.datatype \
           -L$(AROS)/lib -lposix

all: swrt.datatype

swrt.datatype: swrtclass.o
	$(CC) $(CFLAGS) $(INCLUDES) $(LDFLAGS) -o $@ $^

swrtclass.o: swrtclass.c swrt_intern.h
	$(CC) $(CFLAGS) $(INCLUDES) -D__AROS__ -c -o $@ $<

clean:
	rm -f *.o *.datatype

install: swrt.datatype
	cp swrt.datatype $(AROS)/SYS/Classes/DataTypes/
	cp swrt.swrt $(AROS)/SYS/Storage/Datatypes/
	echo "Installed. Reboot AROS to use."
