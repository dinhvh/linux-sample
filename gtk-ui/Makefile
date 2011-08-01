PRE_OBJECTS=../chash.o ../carray.o
OBJECTS=file-reader.o gtk-sample.o etpan-gtk-tree-model.o main-window.o
CPPFLAGS=-W -Wall -g -I.. `pkg-config --cflags gtk+-2.0`
LDFLAGS=`pkg-config --libs gtk+-2.0`

gtk-sample: $(OBJECTS)
	gcc -o $@ $(OBJECTS) $(PRE_OBJECTS) $(LDFLAGS)

clean:
	rm -f $(OBJECTS) gtk-sample *~

.c.o:
	gcc $(CPPFLAGS) -c -o $@ $<
