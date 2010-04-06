CFLAGS := -Wall -O3 -g
LDFLAGS :=

DESTDIR :=
PREFIX := /usr/local
INSTDIR := $(DESTDIR)$(PREFIX)/sbin

MODULES = tuxoniceui
CORE_OBJECTS = userui_core.o

FBSPLASH_LIBS = -lmng -lpng -ljpeg -lz -lfreetype -llcms -lm

USPLASH_LIBS = -lusplash

default: tuxoniceui

fbsplash:
	make -C $@

usplash:
	make -C $@

tuxoniceui: $(CORE_OBJECTS) userui_text.o fbsplash usplash
	$(CC) $(LDFLAGS) userui_core.o userui_text.o fbsplash/userui_fbsplash.o usplash/userui_usplash.o $(FBSPLASH_LIBS) $(USPLASH_LIBS) -o tuxoniceui

clean:
	$(RM) *.o $(TARGETS) fbsplash/*.o usplash/*.o

$(INSTDIR)/%: %
	strip $<
	install -m 755 -o root -g root $< $@

install: tuxoniceui $(INSTDIR)/tuxoniceui

.PHONY: all clean install fbsplash usplash
