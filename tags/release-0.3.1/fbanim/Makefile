CFLAGS = -Wall -O0 -g
TARGET = userui_fbanim.o
LDFLAGS = -r -nostdlib -nostartfiles
OBJECTS = userui_fbanim_core.o splash_render.o splash_common.o splash_unpack.o

all: $(TARGET)

userui_fbanim.o: $(OBJECTS)
	gcc $(LDFLAGS) $(SPLASH_LDLIBS) $^ -o $@

$(OBJECTS): ../userui.h config.h splash.h

clean:
	rm -f *.o $(TARGET)
