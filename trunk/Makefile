CFLAGS := -Wall -O3 -g
LDFLAGS := -static

DESTDIR := 
PREFIX := /usr/local
INSTDIR := $(DESTDIR)/$(PREFIX)/sbin

MODULES = text fbsplash
TARGETS = $(patsubst %,suspend2_ui_%,$(MODULES))
CORE_OBJECTS = userui_core.o

FBSPLASH_LIBS = -lpng -ljpeg -lz -lm -lfreetype

all: fbsplash fbanim $(TARGETS)

fbsplash fbanim:
	make -C $@ all

suspend2_ui_text: $(CORE_OBJECTS) userui_text.o
	$(CC) $(LDFLAGS) $^ -o $@

suspend2_ui_fbsplash: $(CORE_OBJECTS) fbsplash/userui_fbsplash.o
	$(CC) $(LDFLAGS) $^ -o $@ $(FBSPLASH_LIBS)

suspend2_ui_fbanim: $(CORE_OBJECTS) fbanim/userui_fbanim.o
	$(CC) -ljpeg -lpng -lz -lm -lpthread $(LDFLAGS) $^ -o $@

clean:
	rm -f *.o $(TARGETS)
	make -C fbsplash clean
	make -C fbanim clean

$(INSTDIR)/%: %
	install -m 755 -o root -g root $< $@

install: all $(patsubst %,$(INSTDIR)/%,$(TARGETS))

.PHONY: all clean install fbsplash fbanim
