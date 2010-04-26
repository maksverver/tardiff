CFLAGS=-Wall -Wextra -O2 -g
OBJS=common.o binsort.o identify.o tardiff.o \
	tarpatch.o tardiffmerge.o tardiffinfo.o main.o
LDLIBS=-lcrypto -lz

all: tardiff

tardiff: $(OBJS)
	$(CC) $(LDFLAGS) -o tardiff $(OBJS) $(LDLIBS)

install: all
	install -s tardiff $(PREFIX)/bin/
	ln -sf tardiff $(PREFIX)/bin/tarpatch
	ln -sf tardiff $(PREFIX)/bin/tardiffmerge
	ln -sf tardiff $(PREFIX)/bin/tardiffinfo

uninstall:
	rm -f $(PREFIX)/bin/tardiff
	rm -f $(PREFIX)/bin/tarpatch
	rm -f $(PREFIX)/bin/tardiffmerge
	rm -f $(PREFIX)/bin/tardiffinfo

clean:
	rm -f *.o

distclean: clean
	rm -f tardiff

.PHONY: all clean distclean install uninstall
