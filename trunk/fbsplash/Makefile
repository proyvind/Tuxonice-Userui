CFLAGS := -Wall -O3 -g
INCLUDES = -I. -I/usr/include/freetype2/

TARGET = userui_fbsplash.o
OBJECTS = userui_fbsplash_core.o cmd.o common.o image.o list.o parse.o render.o ttf.o

all: $(TARGET)

userui_fbsplash.o: $(OBJECTS)
	$(CC) $(LDFLAGS) -r -nostdlib -nostartfiles $(SPLASH_LDLIBS) $^ -o $@

$(OBJECTS): ../userui.h config.h splash.h
	$(CC) $(CFLAGS) $(INCLUDES) -c $*.c -o $@

clean:
	$(RM) *.o $(TARGET)
