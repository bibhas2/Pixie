CC=gcc
CFLAGS=-std=gnu99 
OBJS=Proxy.o main.o
HEADERS=Proxy.h

all: pixie

%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c -o $@ $<
libpixie.a: $(OBJS) 
	ar rcs libpixie.a $(OBJS)
pixie: $(HEADERS) libpixie.a
	gcc -o pixie -L../Cute -L. -lpixie -lcute -lpthread
clean:
	rm $(OBJS)
	rm -f pixie libpixie.a
