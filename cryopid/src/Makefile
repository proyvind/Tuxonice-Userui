OBJECTS = freeze defrost makeelf

CFLAGS = -g

CHPAX = /sbin/chpax
DEPAX = test ! -x $(CHPAX) || $(CHPAX) -xperms

# Switch the commenting on the following lines if we suspect wacky uclibc bugs
#LIBC = -nostdlib -nostartfiles ../uclibc/libc.a ../uclibc/crt0.o ../uclibc/crti.o
LIBC = -lc

all: $(OBJECTS)
	$(DEPAX) $^

clean:
	rm -f *.o $(OBJECTS)

defrost: process.o defrost.o
	gcc -static -o $@ -Tmylinking.ld $^ $(LIBC)

freeze: process.o freeze.o

stub: stub.o
	gcc -static -nostdlib -fomit-frame-pointer -fpic -Tstub.ld -o $@ $^ $(LIBC)

makeelf: makeelf.o process.o restore.o
