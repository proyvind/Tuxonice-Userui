CFLAGS += -Wall -O3
DESTDIR :=
PREFIX := /usr/local
INSTDIR := $(DESTDIR)$(PREFIX)/sbin

MODULES = tuxoniceui

OBJECTS = userui_core.o userui_text.o
LIBS =

# FBSPLASH
ifdef USE_FBSPLASH
OBJECTS += fbsplash
LIBS += -lmng -lpng -ljpeg -lfreetype -lm
LIB_TARGETS = fbsplash/userui_fbsplash.o
CFLAGS += -DUSE_FBSPLASH
endif

# USPLASH
ifdef USE_USPLASH
OBJECTS += usplash
LIBS += -lusplash usplash/userui_usplash.o
CFLAGS += -DUSE_USPLASH
endif

#PLYMOUTH
ifdef USE_PLYMOUTH
OBJECTS += plymouth
LIBS += -lplymouth plymouth/plymouth.o
CFLAGS += -DUSE_PLYMOUTH
endif

default: tuxoniceui

fbsplash:
	make -C $@

usplash:
	make -C $@

plymouth:
	make -C $@

tuxoniceui: $(OBJECTS)
	$(CC) $(LDFLAGS) userui_core.o userui_text.o $(LIB_TARGETS) $(LIBS) -o tuxoniceui

clean:
	$(RM) *.o $(TARGETS) fbsplash/*.o usplash/*.o plymouth/*.o tuxoniceui

$(INSTDIR)/%: %
	strip $<
	install -D -m 755 -o root -g root $< $@

install: tuxoniceui $(INSTDIR)/tuxoniceui

.PHONY: all clean install fbsplash usplash plymouth
