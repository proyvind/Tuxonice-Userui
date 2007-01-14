CFLAGS := -Wall -O3 -g
LDFLAGS :=

DESTDIR :=
PREFIX := /usr/local
INSTDIR := $(DESTDIR)$(PREFIX)/sbin

MODULES = text fbsplash usplash
TARGETS = $(patsubst %,suspend2ui_%,$(MODULES))
CORE_OBJECTS = userui_core.o

FBSPLASH_LIBS = -lmng -lpng -ljpeg -lz -lfreetype -llcms -lm

USPLASH_LIBS = -ldl

all: fbsplash usplash $(TARGETS)

fbsplash:
	make -C $@ all

usplash:
	make -C $@ all

suspend2ui_text: $(CORE_OBJECTS) userui_text.o
	$(CC) $(LDFLAGS) -static $^ -o $@

suspend2ui_fbsplash: $(CORE_OBJECTS) fbsplash/userui_fbsplash.o
	$(CC) $(LDFLAGS) -static $^ -o $@ $(FBSPLASH_LIBS)

suspend2ui_usplash: $(CORE_OBJECTS) usplash/userui_usplash.o
	$(CC) $(filter-out -static,$(LDFLAGS)) $^ -o $@ $(USPLASH_LIBS)

clean:
	$(RM) *.o $(TARGETS)
	make -C fbsplash clean
	make -C usplash clean

$(INSTDIR)/%: %
	strip $<
	install -m 755 -o root -g root $< $@

install: all $(patsubst %,$(INSTDIR)/%,$(TARGETS))

.PHONY: all clean install fbsplash usplash
