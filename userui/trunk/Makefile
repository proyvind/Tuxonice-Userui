CFLAGS = -Wall -O0 -g
TARGETS = userui_program
UI_OBJECTS = userui_text.o splash/userui_fbsplash.o
OBJECTS = userui_core.o $(UI_OBJECTS)
LDFLAGS = -ljpeg -lpng -lz -lm

all: linux $(TARGETS)

linux:
	@cat README && false

userui_program: $(OBJECTS)
	gcc $(LDFLAGS) $^ -o $@
	strip $@

splash/userui_fbsplash.o:
	make -C splash

$(OBJECTS): userui.h

clean:
	rm -f *.o $(TARGETS)
	make -C splash clean
