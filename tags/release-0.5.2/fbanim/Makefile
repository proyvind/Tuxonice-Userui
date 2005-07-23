CFLAGS := -Wall -O3 -g

TARGET = userui_fbanim.o
OBJECTS = userui_fbanim_core.o mng_render.o mng_callbacks.o
SOURCES = $(patsubst %.o,%.c,$(OBJECTS))

all: $(TARGET)

userui_fbanim.o: $(OBJECTS)
	$(CC) $(LDFLAGS) -r -nostdlib -nostartfiles $(SPLASH_LDLIBS) $^ -o $@

$(OBJECTS): $(SOURCES) ../userui.h fbanim_mng.h
	$(CC) $(CFLAGS) $(INCLUDES) -c $*.c -o $@

clean:
	$(RM) *.o $(TARGET)
