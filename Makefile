CC=gcc
CFLAGS=-std=gnu99 
OBJS=Proxy.o main.o
HEADERS=Proxy.h

all: pixie

%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c -o $@ $<
pixie: $(OBJS) 
	gcc -o pixie $(OBJS) -L../Cute -L. -lcute 
clean:
	rm $(OBJS)
	rm pixie
