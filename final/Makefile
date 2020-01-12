CC=gcc
LD=gcc
CFLAGS=-g -Wall
CPPFLAGS=-I. -I/home/cs417/exercises/ex3/include
SP_LIBRARY=/home/cs417/exercises/ex3/libspread-core.a /home/cs417/exercises/ex3/libspread-util.a

all: server client

.c.o:
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $<

server:  server.o
	$(LD) -o $@ server.o -ldl $(SP_LIBRARY)

client:  client.o
	$(LD) -o $@ client.o -ldl $(SP_LIBRARY)

clean:
	rm -f *.o server client *.log

