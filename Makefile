OBJECTS=stack.o etpan-symbols.o chash.o carray.o
CPPFLAGS=-W -Wall -g -D__FRAME_OFFSETS

all: sample
	cd gtk-ui ; make

sample: $(OBJECTS)
	gcc -o $@ $(OBJECTS) -lbfd -liberty

clean:
	cd gtk-ui ; make clean
	rm -f $(OBJECTS) sample *~

.c.o:
	gcc $(CPPFLAGS) -c -o $@ $<
