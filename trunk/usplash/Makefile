CFLAGS := -g -Wall -fPIC -O3 -D_GNU_SOURCE
INCLUDES = -I. -Ibogl

TARGET = userui_usplash.o
OBJECTS = userui_usplash_core.o \
	usplash-testcard.o usplash-testcard-theme.o \
	libusplash.o usplash_bogl.o bogl/libbogl.a
SOURCES = $(patsubst %.o,%.c,$(OBJECTS))

all: $(TARGET)

userui_usplash.o: bogl $(OBJECTS)
	$(CC) $(LDFLAGS) -r -nostdlib -nostartfiles $(OBJECTS) -o $@

bogl:
	$(MAKE) -C bogl

.c.o: ../userui.h
	$(CC) $(CFLAGS) $(INCLUDES) -c $*.c -o $@

.bdf.c: bogl
	./bogl/bdftobogl $< > $@

.png.c: bogl
	./bogl/pngtobogl $< > $@

clean:
	$(RM) *.o $(TARGET)
	$(MAKE) -C bogl clean

.PHONY: bogl install clean 
.SUFFIXES: .png .bdf
