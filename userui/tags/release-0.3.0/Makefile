CFLAGS = -Wall -O0 -g
MODULES = text fbsplash
TARGETS = $(patsubst %,userui_%,$(MODULES))
CORE_OBJECTS = userui_core.o
LDFLAGS = 
PREFIX = /usr/local

all: $(TARGETS)
	strip $(TARGETS)

userui_dummy: $(CORE_OBJECTS) userui_dummy.o
	$(CC) $(LDFLAGS) $^ -o $@

userui_text: $(CORE_OBJECTS) userui_text.o
	$(CC) $(LDFLAGS) $^ -o $@

userui_fbsplash: $(CORE_OBJECTS) fbsplash/userui_fbsplash.o
	$(CC) -ljpeg -lpng -lz -lm $(LDFLAGS) $^ -o $@

userui_fbanim: $(CORE_OBJECTS) fbanim/userui_fbanim.o
	$(CC) -ljpeg -lpng -lz -lm -lpthread $(LDFLAGS) $^ -o $@

fbsplash/userui_fbsplash.o:
	make -C fbsplash

fbanim/userui_fbanim.o:
	make -C fbanim

clean:
	rm -f *.o $(TARGETS)
	make -C fbsplash clean
	make -C fbanim clean

install:
	install -m 755 -o root -g root userui_fbsplash $(PREFIX)/sbin
	install -m 755 -o root -g root userui_text $(PREFIX)/sbin

.PHONY: all clean
