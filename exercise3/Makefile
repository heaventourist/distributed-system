CC=gcc
LD=gcc
CFLAGS=-g -Wall
CPPFLAGS=-I. -I/home/cs417/exercises/ex3/include
SP_LIBRARY=/home/cs417/exercises/ex3/libspread-core.a /home/cs417/exercises/ex3/libspread-util.a

all: mcast

.c.o:
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $<

mcast:  mcast.o
	$(LD) -o $@ mcast.o -ldl $(SP_LIBRARY)

clean:
	rm -f *.o mcast

