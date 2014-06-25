.SUFFIXES:
    MAKEFLAGS += -r

.PHONY: all
all: diskburn

CXX=g++

DEFS=-D_FILE_OFFSET_BITS=64
CXXFLAGS=-O3 -DNDEBUG -Wall -fopenmp -std=c++11
LDFLAGS=-fopenmp

OBJS=main.o

%.o: %.cpp
	$(CXX) -c $< -o $@ $(CXXFLAGS) $(DEFS)

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
