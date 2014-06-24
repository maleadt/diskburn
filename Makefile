.PHONY: all
all: blocktest

blocktest: main.cpp progress.hpp
	g++ -O3 -lrt $< -o $@ -Wall -std=c++11

.PHONY: clean
clean:
	rm -f blocktest
