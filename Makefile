.SUFFIXES:
    MAKEFLAGS += -r

.PHONY: all
all: diskburn

CXX=g++

DEFS=-D_FILE_OFFSET_BITS=64
DEFS_DEBUG=
DEFS_RELEASE=

CXXFLAGS=-Wall -fopenmp -std=c++11
CXXFLAGS_DEBUG=-DDEBUG -g -O1
CXXFLAGS_RELEASE=-DNDEBUG -O3

LDFLAGS=-fopenmp
LDFLAGS_DEBUG=
LDFLAGS_RELEASE=

HAVE_CLANG := $(shell which clang++ 1>&2 2>/dev/null; echo $$?)
ifeq ($(HAVE_CLANG), 0)
	CXX=$(shell which clang++)
	CXXFLAGS_DEBUG += -fsanitize=address
	LDFLAGS_DEBUG += -fsanitize=address
endif

.PHONY: all debug release
debug: DEFS += $(DEFS_DEBUG)
debug: CXXFLAGS += $(CXXFLAGS_DEBUG)
debug: LDFLAGS += $(LDFLAGS_DEBUG)
debug: all
release: DEFS += $(DEFS_RELEASE)
release: CXXFLAGS += $(CXXFLAGS_RELEASE)
release: LDFLAGS += $(LDFLAGS_RELEASE)
release: all

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
