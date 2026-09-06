CXX ?= c++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -pedantic

.PHONY: all check clean

all: moss

moss: src/moss.cpp
	$(CXX) $(CXXFLAGS) $< -o $@

check: moss
	mkdir -p build
	./moss --check examples/counter.moss
	./moss examples/counter.moss -o build/counter.rs
	rustc build/counter.rs -o build/counter
	./build/counter

clean:
	rm -f moss build/counter build/counter.rs
