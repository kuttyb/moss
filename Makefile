CXX ?= c++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -pedantic

.PHONY: all check clean

all: moss

moss: src/moss.cpp
	$(CXX) $(CXXFLAGS) $< -o $@

check: moss
	sh tests/run.sh ./moss build/tests

clean:
	rm -f moss build/counter build/counter.rs build/checkout build/checkout.rs
	rm -rf build/tests
