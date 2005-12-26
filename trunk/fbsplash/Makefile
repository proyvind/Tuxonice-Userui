CFLAGS := -Wall -O3 -g
INCLUDES = -I/usr/include/freetype2/ -I.

TARGET = userui_fbsplash.o
OBJECTS = userui_fbsplash_core.o cmd.o common.o effects.o image.o list.o \
		parse.o mng_callbacks.o mng_render.o render.o ttf.o
SOURCES = $(patsubst %.o,%.c,$(OBJECTS))

all: $(TARGET)

userui_fbsplash.o: $(OBJECTS)
	$(CC) $(LDFLAGS) -r -nostdlib -nostartfiles $(SPLASH_LDLIBS) $^ -o $@

%.o: %.c ../userui.h config.h splash.h
	$(CC) $(CFLAGS) $(INCLUDES) -c $*.c -o $@

clean:
	$(RM) *.o $(TARGET)
