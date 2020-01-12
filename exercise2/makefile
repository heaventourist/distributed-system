CC=gcc
CXX=g++
CXXFLAGS = -g -c -Wall -pedantic -std=c++11
CFLAGS = -g -c -Wall -pedantic

all: mcast start_mcast

mcast: recv_dbg.o mcast.o
	$(CXX) -o mcast recv_dbg.o mcast.o

start_mcast: start_mcast.o
	$(CXX) -o start_mcast start_mcast.o

clean:
	rm *.o
	rm mcast start_mcast

%.o: %.c
	$(CC) $(CFLAGS) $*.c

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $*.cpp