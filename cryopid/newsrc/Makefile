OBJECTS = freeze stub

CFLAGS = -g

CHPAX = /sbin/chpax
DEPAX = test ! -x $(CHPAX) || $(CHPAX) -xperms

# Switch the commenting on the following lines if we suspect wacky uclibc bugs
LIBC = -nostdlib -nostartfiles ../uclibc/libc.a ../uclibc/crt0.o ../uclibc/crti.o
#LIBC = -lc

all: $(OBJECTS)

clean:
	rm -f *.o $(OBJECTS) stub-image.S

stub-image.S: stub src2asm.pl
	perl src2asm.pl < $< > $@

freeze: freeze.o process.o imagewriter.o elfwriter.o stub-image.o

stub: stub.o stub-linking.x
	gcc -static -Tstub-linking.x -o $@ $< $(LIBC)
	strip $@

