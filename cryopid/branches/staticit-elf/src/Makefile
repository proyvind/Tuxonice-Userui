OBJECTS = make-static

CFLAGS = -g -Wall -O3

CHPAX = /sbin/chpax
DEPAX = test ! -x $(CHPAX) || $(CHPAX) -xperms

# Switch the commenting on the following lines if we suspect wacky uclibc bugs
LIBC = -nostdlib -nostartfiles ../uclibc/libc.a ../uclibc/crt0.o ../uclibc/crti.o
#LIBC = -lc

all: $(OBJECTS)

make-static.o imagewriter.o process.o: process.h

clean:
	rm -f *.o $(OBJECTS)

make-static: make-static.o process.o imagewriter.o

stub: stub.c supervisor.c
	gcc -g -static -Tstub-linking.x -Os -o $@ $^ $(LIBC)
	#strip $@
	$(DEPAX) $@

