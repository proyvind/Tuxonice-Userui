CFLAGS = -Wall -O2
TARGET = userui_program
OBJECTS = userui_text.o userui_core.o splash/userui_fbsplash.o
LDFLAGS = -ljpeg -lpng -lz -lm

all: $(TARGET) linux

linux:
	@[ ! -d linux ] && cat README && false

userui_program: $(OBJECTS)
	gcc $(LDFLAGS) $^ -o $@
	strip $@

splash/userui_fbsplash.o:
	make -C splash

$(OBJECTS): userui.h

clean:
	rm -f *.o $(TARGET)
	make -C splash clean
