ifeq ($(origin CXX),default)
CXX := clang++
endif
CLANG_GCC_INSTALL_DIR ?= $(shell machine=`gcc -dumpmachine 2>/dev/null`; \
	for dir in /usr/lib/gcc/$$machine/*; do \
		version=$${dir##*/}; \
		{ [ -d "$$dir/include/c++" ] || [ -d "/usr/include/c++/$$version" ]; } && \
			printf '%s\n' "$$dir"; \
	done | sort -V | tail -n 1)
ifneq ($(filter clang++ clang++-%,$(notdir $(firstword $(CXX)))),)
CXX_TOOLCHAIN_FLAGS ?= $(if $(CLANG_GCC_INSTALL_DIR),--gcc-install-dir=$(CLANG_GCC_INSTALL_DIR))
else
CXX_TOOLCHAIN_FLAGS ?=
endif
CXXFLAGS_BASE ?= -std=c++17 -Wall -Wextra -pedantic
CXXFLAGS_DEV ?= $(CXXFLAGS_BASE) -O0
CXXFLAGS_OPT ?= $(CXXFLAGS_BASE) -O2
CPPFLAGS ?=
LDFLAGS ?= -fuse-ld=mold

COMPILER_DIR = build/compiler
MOSS_DEV = $(COMPILER_DIR)/moss-dev
MOSS_OPT = $(COMPILER_DIR)/moss-opt
MOSS_RELEASE = $(COMPILER_DIR)/moss-release
COMPILER_SOURCES := src/main.cpp src/diagnostic_codes.cpp src/moss.cpp
DEV_OBJECT_DIR := $(COMPILER_DIR)/obj/dev
OPT_OBJECT_DIR := $(COMPILER_DIR)/obj/opt
DEV_OBJECTS := $(patsubst src/%.cpp,$(DEV_OBJECT_DIR)/%.o,$(COMPILER_SOURCES))
OPT_OBJECTS := $(patsubst src/%.cpp,$(OPT_OBJECT_DIR)/%.o,$(COMPILER_SOURCES))
DEV_CONFIG := $(COMPILER_DIR)/config/dev.flags
OPT_CONFIG := $(COMPILER_DIR)/config/opt.flags

MOSS ?= ./moss
RUSTC ?= rustc
EXAMPLE_BUILD_DIR ?= build/examples/optimized
TEST_JOBS ?= $(shell nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 16)

.PHONY: all moss moss-dev moss-opt moss-release check check-parallel check-serial examples examples-optimized clean help FORCE

# Default target: build fast development compiler (-O0) and update ./moss
all: moss-dev

moss: moss-dev

moss-dev: $(MOSS_DEV)
	@cp -f $(MOSS_DEV) moss

$(DEV_CONFIG): FORCE
	@mkdir -p $(@D)
	@{ printf '%s\n' 'CXX=$(CXX)' 'CXX_TOOLCHAIN_FLAGS=$(CXX_TOOLCHAIN_FLAGS)' 'CPPFLAGS=$(CPPFLAGS)' 'CXXFLAGS_DEV=$(CXXFLAGS_DEV)' 'LDFLAGS=$(LDFLAGS)' 'COMPILER=$(shell $(CXX) --version 2>/dev/null | head -n 1)' 'TARGET=$(shell $(CXX) -dumpmachine 2>/dev/null)'; } > $@.tmp
	@if ! cmp -s $@.tmp $@; then mv -f $@.tmp $@; else rm -f $@.tmp; fi

$(OPT_CONFIG): FORCE
	@mkdir -p $(@D)
	@{ printf '%s\n' 'CXX=$(CXX)' 'CXX_TOOLCHAIN_FLAGS=$(CXX_TOOLCHAIN_FLAGS)' 'CPPFLAGS=$(CPPFLAGS)' 'CXXFLAGS_OPT=$(CXXFLAGS_OPT)' 'LDFLAGS=$(LDFLAGS)' 'COMPILER=$(shell $(CXX) --version 2>/dev/null | head -n 1)' 'TARGET=$(shell $(CXX) -dumpmachine 2>/dev/null)'; } > $@.tmp
	@if ! cmp -s $@.tmp $@; then mv -f $@.tmp $@; else rm -f $@.tmp; fi

$(DEV_OBJECT_DIR)/%.o: src/%.cpp $(DEV_CONFIG)
	@mkdir -p $(@D)
	$(CXX) $(CXX_TOOLCHAIN_FLAGS) $(CPPFLAGS) $(CXXFLAGS_DEV) -MMD -MP -MF $(@:.o=.d) -MT $@ -c $< -o $@

$(OPT_OBJECT_DIR)/%.o: src/%.cpp $(OPT_CONFIG)
	@mkdir -p $(@D)
	$(CXX) $(CXX_TOOLCHAIN_FLAGS) $(CPPFLAGS) $(CXXFLAGS_OPT) -MMD -MP -MF $(@:.o=.d) -MT $@ -c $< -o $@

$(MOSS_DEV): $(DEV_OBJECTS) $(DEV_CONFIG)
	$(CXX) $(CXX_TOOLCHAIN_FLAGS) $(CXXFLAGS_DEV) $(LDFLAGS) $(DEV_OBJECTS) -o $@

moss-opt: $(MOSS_OPT)

moss-release: $(MOSS_RELEASE)

$(MOSS_RELEASE): $(MOSS_OPT)
	@cp -f $(MOSS_OPT) $@

$(MOSS_OPT): $(OPT_OBJECTS) $(OPT_CONFIG)
	$(CXX) $(CXX_TOOLCHAIN_FLAGS) $(CXXFLAGS_OPT) $(LDFLAGS) $(OPT_OBJECTS) -o $@

FORCE:

-include $(DEV_OBJECTS:.o=.d) $(OPT_OBJECTS:.o=.d)

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
