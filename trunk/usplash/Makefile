CFLAGS := -Wall -O3 -g -D_GNU_SOURCE -DBOGL_CFB_FB=1 -DBOGL_VGA16_FB=1
INCLUDES = -I.

TARGET = userui_usplash.o
OBJECTS = userui_usplash_core.o \
		bogl.o bogl-bgf.o bogl-cfb.o bogl-font.o bogl-pcfb.o bogl-tcfb.o \
		bogl-vga16.o \
		helvB10.o
SOURCES = $(patsubst %.o,%.c,$(OBJECTS))

all: $(TARGET)

userui_usplash.o: $(OBJECTS)
	$(CC) $(LDFLAGS) -r -nostdlib -nostartfiles $^ -o $@

%.o: %.c ../userui.h
	$(CC) $(CFLAGS) $(INCLUDES) -c $*.c -o $@

clean:
	$(RM) *.o $(TARGET) bdftobogl

bdftobogl: bogl.o bogl-font.o bogl-vga16.o bogl-cfb.o bogl-pcfb.o bogl-tcfb.o
%.c: %.bdf bdftobogl
	./bdftobogl $< > $@
