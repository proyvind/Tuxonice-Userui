CFLAGS := -Wall -O0 -g
LDFLAGS := -static

DESTDIR := 
PREFIX := /usr/local
INSTDIR := $(DESTDIR)/$(PREFIX)/sbin

MODULES = text fbsplash
TARGETS = $(patsubst %,userui_%,$(MODULES))
CORE_OBJECTS = userui_core.o

FBSPLASH_LIBS = -lpng -ljpeg -lz -lm -lfreetype

all: $(TARGETS)

userui_text: $(CORE_OBJECTS) userui_text.o
	$(CC) $(LDFLAGS) $^ -o $@

userui_fbsplash: $(CORE_OBJECTS) fbsplash/userui_fbsplash.o
	$(CC) $(LDFLAGS) $^ -o $@ $(FBSPLASH_LIBS)

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

$(INSTDIR)/%: %
	install -m 755 -o root -g root $< $@

install: all $(patsubst %,$(INSTDIR)/%,$(TARGETS))

.PHONY: all clean install
