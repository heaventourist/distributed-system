CC=gcc

CFLAGS = -g -c -Wall -pedantic

all: ncp rcv t_ncp t_rcv

ncp: sendto_dbg.o util.o ncp.o
	$(CC) -o ncp sendto_dbg.o util.o ncp.o

rcv: sendto_dbg.o util.o rcv.o
	$(CC) -o rcv sendto_dbg.o util.o rcv.o

t_rcv: t_rcv.o
	$(CC) -o t_rcv t_rcv.o

t_ncp: t_ncp.o
	$(CC) -o t_ncp t_ncp.o

clean:
	rm *.o
	rm ncp rcv t_ncp t_rcv

%.o: %.c
	$(CC) $(CFLAGS) $*.c