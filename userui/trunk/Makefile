CFLAGS = -Wall -O0 -g
MODULES = dummy text fbsplash
TARGETS = $(patsubst %,userui_%,$(MODULES))
CORE_OBJECTS = userui_core.o
LDFLAGS = 

all: linux $(TARGETS)
	strip $(TARGETS)

linux:
	@cat README && false

userui_dummy: $(CORE_OBJECTS) userui_dummy.o
	gcc $(LDFLAGS) $^ -o $@

userui_text: $(CORE_OBJECTS) userui_text.o
	gcc $(LDFLAGS) $^ -o $@

userui_fbsplash: $(CORE_OBJECTS) splash/userui_fbsplash.o
	gcc -ljpeg -lpng -lz -lm $(LDFLAGS) $^ -o $@

splash/userui_fbsplash.o:
	make -C splash

clean:
	rm -f *.o $(TARGETS)
	make -C splash clean

.PHONY: all clean
