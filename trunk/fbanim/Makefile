CFLAGS := -Wall -O0 -g

TARGET = userui_fbanim.o
OBJECTS = userui_fbanim_core.o splash_render.o splash_common.o splash_unpack.o

all: $(TARGET)

userui_fbanim.o: $(OBJECTS)
	$(CC) $(LDFLAGS) -r -nostdlib -nostartfiles $(SPLASH_LDLIBS) $^ -o $@

$(OBJECTS): ../userui.h config.h splash.h
	$(CC) $(CFLAGS) $(INCLUDES) -c $*.c -o $@

clean:
	$(RM) *.o $(TARGET)
