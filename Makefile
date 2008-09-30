CFLAGS=-Wall -Wextra -O2
TARDIFF_OBJS=tardiff.o common.o binsort.o
TARPATCH_OBJS=tarpatch.o common.o
TARDIFFMERGE_OBJS=tardiffmerge.o common.o
LDLIBS=-lcrypto -lz

all: tardiff tarpatch tardiffmerge

tardiff: $(TARDIFF_OBJS)
	$(CC) $(LDFLAGS) -o tardiff $(TARDIFF_OBJS) $(LDLIBS)

tarpatch: $(TARPATCH_OBJS)
	$(CC) $(LDFLAGS) -o tarpatch $(TARPATCH_OBJS) $(LDLIBS)

tardiffmerge: $(TARDIFFMERGE_OBJS)
	$(CC) $(LDFLAGS) -o tardiffmerge $(TARDIFFMERGE_OBJS) $(LDLIBS)

install: all
	install -s tardiff $(PREFIX)/bin
	install -s tarpatch $(PREFIX)/bin
	install -s tardiffmerge $(PREFIX)/bin

clean:
	rm -f *.o

distclean: clean
	rm -f tardiff tarpatch tardiffmerge

.PHONY: all clean distclean install
