CFLAGS := -Wall -O3 -g
LDFLAGS :=

DESTDIR :=
PREFIX := /usr/local
INSTDIR := $(DESTDIR)$(PREFIX)/sbin

MODULES = text fbsplash usplash
TARGETS = $(patsubst %,suspend2ui_%,$(MODULES))
CORE_OBJECTS = userui_core.o

FBSPLASH_LIBS = -lmng -lpng -ljpeg -lz -lfreetype -llcms -lm

USPLASH_LIBS = -lusplash

default:
	@echo "There are currently three possible Suspend 2 user interfaces."
	@echo "You may compile all of these by typing \"make all\", or just"
	@echo "one of these by typing one of the following:"
	@echo ""
	@echo "   make suspend2ui_text"
	@echo "       - No extra dependencies are required to build this target."
	@echo ""
	@echo "   make suspend2ui_fbsplash"
	@echo "       - To build this, you must have installed the development"
	@echo "         files for the following libraries:"
	@echo "           libpng, libz, libjpeg, freetype2, lcms, libmng-1.0.5 or later"
	@echo ""
	@echo "   make suspend2ui_usplash"
	@echo "       - To build this, you must have libusplash0 installed."

all: $(TARGETS)

fbsplash:
	make -C $@ all

usplash:
	make -C $@ all

suspend2ui_text: $(CORE_OBJECTS) userui_text.o
	$(CC) $(LDFLAGS) $^ -o $@

suspend2ui_fbsplash: fbsplash $(CORE_OBJECTS) fbsplash/userui_fbsplash.o
	$(CC) $(LDFLAGS) -static $(CORE_OBJECTS) fbsplash/userui_fbsplash.o -o $@ $(FBSPLASH_LIBS)

suspend2ui_usplash: usplash $(CORE_OBJECTS) usplash/userui_usplash.o
	$(CC) $(filter-out -static,$(LDFLAGS)) $(CORE_OBJECTS) usplash/userui_usplash.o -o $@ $(USPLASH_LIBS)

clean:
	$(RM) *.o $(TARGETS)
	make -C fbsplash clean
	make -C usplash clean

$(INSTDIR)/%: %
	strip $<
	install -m 755 -o root -g root $< $@

install: all $(patsubst %,$(INSTDIR)/%,$(TARGETS))

.PHONY: all clean install fbsplash usplash
