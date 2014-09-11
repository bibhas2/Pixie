CC=gcc
CFLAGS=-std=gnu99 
OBJS=Proxy.o Persistence.o 
HEADERS=Proxy.h Persistence.h

all: pixie

%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c -o $@ $<
libpixie.a: $(OBJS)
	ar rcs libpixie.a $(OBJS)
pixie: $(HEADERS) main.o libpixie.a
	gcc -o pixie main.o -L../Cute -L. -lpixie -lcute -lpthread
clean:
	rm $(OBJS)
	rm main.o
	rm -f pixie libpixie.a
