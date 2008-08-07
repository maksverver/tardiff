CFLAGS=-Wall -Wextra -I. -g -O2
TARDIFF_OBJS=tardiff.o common.o
TARPATCH_OBJS=tarpatch.o common.o
LDLIBS=-lcrypto -lz

all: tardiff tarpatch

tardiff: $(TARDIFF_OBJS)
	$(CC) $(LDFLAGS) -o tardiff $(TARDIFF_OBJS) $(LDLIBS)

tarpatch: $(TARPATCH_OBJS)
	$(CC) $(LDFLAGS) -o tarpatch $(TARPATCH_OBJS) $(LDLIBS)

clean:
	rm -f *.o

distclean: clean
	rm -f tardiff tarpatch

.PHONY: all clean distclean
