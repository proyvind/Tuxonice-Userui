all: userui_program

CFLAGS = -Wall -O2

TARGET = userui_program
OBJECTS = userui_text.o userui_core.o userui_fbsplash.o

userui_program: $(OBJECTS)
	gcc $(LDFLAGS) $^ -o $@
	strip $@

$(OBJECTS): userui.h

clean:
	rm -f *.o $(TARGET)
