CFLAGS := -Wall -O0 -g
LDFLAGS := -r -nostdlib -nostartfiles

TARGET = userui_fbsplash.o
OBJECTS = userui_fbsplash_core.o splash_cmd.o splash_common.o splash_parse.o splash_render.o splash_unpack.o

all: $(TARGET)

userui_fbsplash.o: $(OBJECTS)
	gcc $(LDFLAGS) $(SPLASH_LDLIBS) $^ -o $@

$(OBJECTS): ../userui.h config.h splash.h

clean:
	rm -f *.o $(TARGET)
