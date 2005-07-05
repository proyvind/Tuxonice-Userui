CFLAGS := -Wall -O3 -g
LDFLAGS := -static

DESTDIR :=
PREFIX := /usr/local
INSTDIR := $(DESTDIR)$(PREFIX)/sbin

MODULES = text fbsplash
TARGETS = $(patsubst %,suspend2ui_%,$(MODULES))
CORE_OBJECTS = userui_core.o

FBSPLASH_LIBS = -lmng -lpng -ljpeg -lz -lfreetype -llcms -lm

all: fbsplash $(TARGETS)

fbsplash:
	make -C $@ all

suspend2ui_text: $(CORE_OBJECTS) userui_text.o
	$(CC) $(LDFLAGS) $^ -o $@

suspend2ui_fbsplash: $(CORE_OBJECTS) fbsplash/userui_fbsplash.o
	$(CC) $(LDFLAGS) $^ -o $@ $(FBSPLASH_LIBS)

clean:
	$(RM) *.o $(TARGETS)
	make -C fbsplash clean

$(INSTDIR)/%: %
	strip $<
	install -m 755 -o root -g root $< $@

install: all $(patsubst %,$(INSTDIR)/%,$(TARGETS))

.PHONY: all clean install fbsplash
