CFLAGS := -Wall -O0 -g -I. -I/usr/include/freetype2/
LDFLAGS := -r -nostdlib -nostartfiles

TARGET = userui_fbsplash.o
OBJECTS = userui_fbsplash_core.o cmd.o common.o image.o list.c parse.o render.o ttf.o

all: $(TARGET)

userui_fbsplash.o: $(OBJECTS)
	gcc $(LDFLAGS) $(SPLASH_LDLIBS) $^ -o $@

$(OBJECTS): ../userui.h config.h splash.h

clean:
	rm -f *.o $(TARGET)
