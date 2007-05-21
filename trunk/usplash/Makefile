CFLAGS := -g -Wall -fPIC -O3 -D_GNU_SOURCE
INCLUDES = -I.

TARGET = userui_usplash.o
OBJECTS = userui_usplash_core.o
SOURCES = $(patsubst %.o,%.c,$(OBJECTS))

all: $(TARGET)

userui_usplash.o: $(OBJECTS)
	$(CC) $(LDFLAGS) -r -nostdlib -nostartfiles $(OBJECTS) -o $@

.c.o: ../userui.h
	$(CC) $(CFLAGS) $(INCLUDES) -c $*.c -o $@

clean:
	$(RM) *.o $(TARGET)

.PHONY: install clean 
