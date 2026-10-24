# Clang is the Phase 25.2 default; preserve command-line and environment overrides.
# GNU Make sets built-in CXX=g++, so CXX ?= clang++ would not take effect.
ifeq ($(origin CXX),default)
CXX := clang++
else ifeq ($(origin CXX),undefined)
CXX := clang++
endif
CXXFLAGS_BASE ?= -std=c++17 -Wall -Wextra -pedantic
CXXFLAGS_DEV ?= $(CXXFLAGS_BASE) -O0
CXXFLAGS_OPT ?= $(CXXFLAGS_BASE) -O2
# Link with mold through the compiler driver; override with MOSS_LDFLAGS=.
MOSS_LDFLAGS ?= -fuse-ld=mold

COMPILER_DIR = build/compiler
MOSS_DEV = $(COMPILER_DIR)/moss-dev
MOSS_OPT = $(COMPILER_DIR)/moss-opt
MOSS_RELEASE = $(COMPILER_DIR)/moss-release

MOSS ?= ./moss
RUSTC ?= rustc
EXAMPLE_BUILD_DIR ?= build/examples/optimized
TEST_JOBS ?= $(shell nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 16)

.PHONY: all moss moss-dev moss-opt moss-release check check-parallel check-serial examples examples-optimized clean help

# Default target: build fast development compiler (-O0) and update ./moss
all: moss-dev

moss: moss-dev

moss-dev: $(MOSS_DEV)
	@cp -f $(MOSS_DEV) moss

$(MOSS_DEV): src/moss.cpp $(wildcard src/*.hpp src/*.inc)
	@mkdir -p $(COMPILER_DIR)
	$(CXX) $(CXXFLAGS_DEV) $< -o $@ $(MOSS_LDFLAGS)

moss-opt: $(MOSS_OPT)

moss-release: $(MOSS_RELEASE)

$(MOSS_RELEASE): $(MOSS_OPT)
	@cp -f $(MOSS_OPT) $@

$(MOSS_OPT): src/moss.cpp $(wildcard src/*.hpp src/*.inc)
	@mkdir -p $(COMPILER_DIR)
	$(CXX) $(CXXFLAGS_OPT) $< -o $@ $(MOSS_LDFLAGS)

check: check-parallel

check-parallel: moss-dev moss-opt
	python3 tests/run_parallel.py --jobs $(TEST_JOBS) --compiler-dev $(MOSS_DEV) --compiler-opt $(MOSS_OPT)

check-serial: moss
	sh tests/run.sh ./moss build/tests

examples: examples-optimized

examples-optimized: moss
	@set -eu; \
	out="$(EXAMPLE_BUILD_DIR)"; \
	mkdir -p "$$out"; \
	for source in examples/*.moss; do \
		case "$$source" in \
			examples/use_after_transfer.moss) \
				echo "skipping intentional negative example: $$source"; \
				continue ;; \
		esac; \
		name=$$(basename "$$source" .moss); \
		echo "building $$source with -Oshared-memory"; \
		"$(MOSS)" -Oshared-memory "$$source" -o "$$out/$$name.rs"; \
		"$(RUSTC)" -D warnings "$$out/$$name.rs" -o "$$out/$$name"; \
	done

clean:
	rm -f moss build/counter build/counter.rs build/checkout build/checkout.rs
	rm -f build/object_pipeline build/object_pipeline.rs
	rm -f build/use_after_transfer build/use_after_transfer.rs
	rm -rf build/tests build/examples build/compiler

help:
	@echo "Available Makefile targets:"
	@echo "  make              - Build fast development compiler (-O0) and update ./moss (default)"
	@echo "  make moss-dev     - Build development compiler (-O0) in build/compiler/moss-dev"
	@echo "  make moss-opt     - Build optimized compiler (-O2) in build/compiler/moss-opt"
	@echo "  make moss-release - Alias for moss-opt"
	@echo "  make check        - Run parallel test suite (check-parallel)"
	@echo "  make check-parallel - Run tests concurrently with tests/run_parallel.py (TEST_JOBS=N)"
	@echo "  make check-serial - Run original serial regression runner (tests/run.sh)"
	@echo "  make examples     - Build and verify optimized examples"
	@echo "  make clean        - Remove build artifacts and binaries"
