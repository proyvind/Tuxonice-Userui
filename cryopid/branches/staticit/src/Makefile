OBJECTS = freeze

CFLAGS = -g -Wall -O3

CHPAX = /sbin/chpax
DEPAX = test ! -x $(CHPAX) || $(CHPAX) -xperms

# Switch the commenting on the following lines if we suspect wacky uclibc bugs
LIBC = -nostdlib -nostartfiles ../uclibc/libc.a ../uclibc/crt0.o ../uclibc/crti.o
#LIBC = -lc

all: $(OBJECTS)

elfwriter.o freeze.o imagewriter.o process.o stub.o: process.h

clean:
	rm -f *.o $(OBJECTS) stub-image.S stub

stub-image.S: stub src2asm.pl
	perl src2asm.pl < $< > $@

freeze: freeze.o process.o imagewriter.o elfwriter.o stub-image.o

stub: stub.c supervisor.c
	gcc -static -Tstub-linking.x -Os -o $@ $^ $(LIBC)
	strip $@
	$(DEPAX) $@

