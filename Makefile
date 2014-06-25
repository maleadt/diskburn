.SUFFIXES:
    MAKEFLAGS += -r

.PHONY: all
all: diskburn

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

diskburn: $(OBJS)
	$(CXX) -o $@ $? $(LDFLAGS)

.PHONY: clean
clean:
	rm -f diskburn $(OBJS)

.PHONY: loop
loop:
	dd if=/dev/zero bs=1M count=128 of=/tmp/loop.img
	sudo losetup -v /dev/loop0 /tmp/loop.img
	sudo chown $USER /dev/loop0
