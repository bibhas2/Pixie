CC=gcc
CFLAGS=-std=gnu99 `pkg-config --cflags gtk+-3.0`
OBJS=ui.o
HEADERS=Proxy.h

all: pixie

%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c -o $@ $<
pixie: $(OBJS) 
	gcc -o pixie $(OBJS) -L../../../Cute -L../../ `pkg-config --libs gtk+-3.0` -lpixie -lcute -lpthread
clean:
	rm $(OBJS)
	rm pixie
