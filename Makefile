CFLAGS=-Wall -O2

build: lc3.c
	cc $(CFLAGS) lc3.c -o lc3

clean:
	rm lc3
