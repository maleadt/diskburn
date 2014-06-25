.SUFFIXES:
    MAKEFLAGS += -r

.PHONY: all
all: blocktest

CC=gcc
CXX=g++

CFLAGS=-O2 -Wall
CXXFLAGS=-O2 -Wall -std=c++11
LDFLAGS=-lrt

OBJS=main.o xxhash.o

%.o: %.c
	$(CC) -c $< -o $@ $(CFLAGS)

%.o: %.cpp
	$(CXX) -c $< -o $@ $(CXXFLAGS)

blocktest: $(OBJS)
	$(CXX) -o $@ $? $(LDFLAGS)

.PHONY: clean
clean:
	rm -f blocktest $(OBJS)
