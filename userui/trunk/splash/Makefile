CFLAGS = -Wall -O2
TARGET = userui_fbsplash.o
LDFLAGS = -r -nostdlib -nostartfiles
OBJECTS = userui_fbsplash_core.o splash_render.o splash_common.o splash_unpack.o

all: $(TARGET)

userui_fbsplash.o: $(OBJECTS)
	gcc $(LDFLAGS) $(SPLASH_LDLIBS) $^ -o $@

$(OBJECTS): ../userui.h config.h splash.h

clean:
	rm -f *.o $(TARGET)
