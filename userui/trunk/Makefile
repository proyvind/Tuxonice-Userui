CFLAGS = -Wall -O0 -g
#TARGETS = userui_program dummy_test
TARGETS = userui_program
UI_OBJECTS = userui_text.o splash/userui_fbsplash.o
OBJECTS = userui_core.o $(UI_OBJECTS)
DUMMY_OBJECTS = dummy_test.o $(UI_OBJECTS)
LDFLAGS = -ljpeg -lpng -lz -lm

all: linux $(TARGETS)

linux:
	@cat README && false

userui_program: $(OBJECTS)
	gcc $(LDFLAGS) $^ -o $@
	strip $@

dummy_test: $(DUMMY_OBJECTS)
	gcc $(LDFLAGS) $^ -o $@

splash/userui_fbsplash.o:
	make -C splash

$(OBJECTS): userui.h

clean:
	rm -f *.o $(TARGETS)
	make -C splash clean
