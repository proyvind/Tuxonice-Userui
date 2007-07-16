CFLAGS := -Wall -O3 -g
LDFLAGS :=

DESTDIR :=
PREFIX := /usr/local
INSTDIR := $(DESTDIR)$(PREFIX)/sbin

MODULES = text fbsplash usplash
TARGETS = $(patsubst %,tuxoniceui_%,$(MODULES))
CORE_OBJECTS = userui_core.o

FBSPLASH_LIBS = -lmng -lpng -ljpeg -lz -lfreetype -llcms -lm

USPLASH_LIBS = -lusplash

default:
	@echo "There are currently three possible TuxOnIce user interfaces."
	@echo "You may compile all of these by typing \"make all\", or just"
	@echo "one of these by typing one of the following:"
	@echo ""
	@echo "   make tuxoniceui_text"
	@echo "       - No extra dependencies are required to build this target."
	@echo ""
	@echo "   make tuxoniceui_fbsplash"
	@echo "       - To build this, you must have installed the development"
	@echo "         files for the following libraries:"
	@echo "           libpng, libz, libjpeg, freetype2, lcms, libmng-1.0.5 or later"
	@echo ""
	@echo "   make tuxoniceui_usplash"
	@echo "       - To build this, you must have libusplash0 installed."

all: $(TARGETS)

fbsplash:
	make -C $@ all

usplash:
	make -C $@ all

tuxoniceui_text: $(CORE_OBJECTS) userui_text.o
	$(CC) $(LDFLAGS) -static $^ -o $@

tuxoniceui_fbsplash: fbsplash $(CORE_OBJECTS) fbsplash/userui_fbsplash.o
	$(CC) $(LDFLAGS) -static $(CORE_OBJECTS) fbsplash/userui_fbsplash.o -o $@ $(FBSPLASH_LIBS)

tuxoniceui_usplash: usplash $(CORE_OBJECTS) usplash/userui_usplash.o
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
