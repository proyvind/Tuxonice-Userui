CFLAGS=-g -Wall

OBJECTS = savestate resume sizes

CHPAX = /sbin/chpax
DEPAX = test -x $(CHPAX) && $(CHPAX) -xperms 

all: $(OBJECTS)

clean:
	rm -f *.o $(OBJECTS)

savestate: savestate.c process.o
	$(CC) $(CFLAGS) -o $@ $^
	$(DEPAX) $@

sizes: savestate.c process.o
	$(CC) $(CFLAGS) -DSHOW_SIZES -o $@ $< 
	$(DEPAX) $@

resume: resume.asm
	nasm -o $@  $<
	$(DEPAX) $@
	chmod 755 $@
