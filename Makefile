CFLAGS=-Wall -Wextra -g -O2
TARDIFF_OBJS=tardiff.o common.o
TARPATCH_OBJS=tarpatch.o common.o
LDLIBS=-lcrypto -lz

all: tardiff tarpatch

tardiff: $(TARDIFF_OBJS)
	$(CC) $(LDFLAGS) -o tardiff $(TARDIFF_OBJS) $(LDLIBS)

tarpatch: $(TARPATCH_OBJS)
	$(CC) $(LDFLAGS) -o tarpatch $(TARPATCH_OBJS) $(LDLIBS)

install: all
	install tardiff $(PREFIX)/bin
	install tarpatch $(PREFIX)/bin

clean:
	rm -f *.o

distclean: clean
	rm -f tardiff tarpatch

.PHONY: all clean distclean install
