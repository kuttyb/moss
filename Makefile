# Clang is the Phase 25.2 default; preserve command-line and environment overrides.
# GNU Make sets built-in CXX=g++, so CXX ?= clang++ would not take effect.
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

COMPILER_DIR ?= build/compiler
MOSS_DEV = $(COMPILER_DIR)/moss-dev
MOSS_OPT = $(COMPILER_DIR)/moss-opt
MOSS_RELEASE = $(COMPILER_DIR)/moss-release
COMPILER_SOURCES ?= src/char_string_ordering_runtime.cpp src/checked_body_ir.cpp src/diagnostic_identity.cpp src/display_runtime.cpp src/est/expression_tree.cpp src/est/expression_query.cpp src/executor_runtime.cpp src/fileio_runtime.cpp src/float_format_runtime.cpp src/functional_optimizer.cpp src/handler_runtime.cpp src/native_generator.cpp src/numeric_literal.cpp src/numeric_runtime.cpp src/parser.cpp src/program_identity.cpp src/semantic_checker.cpp src/typed_semantic_api.cpp src/type_ir.cpp src/moss.cpp

# Profiles and command-line configurations use distinct object trees.
COMPILER_CONFIG_DEV = $(shell printf '%s\n' '$(CXX)' '$(CXX_TOOLCHAIN_FLAGS)' '$(CPPFLAGS)' '$(CXXFLAGS_DEV)' '$(CXXFLAGS_PCH)' '$(shell $(CXX) --version 2>/dev/null | head -n 1)' '$(shell $(CXX) -dumpmachine 2>/dev/null)' | cksum | cut -d' ' -f1)
COMPILER_CONFIG_OPT = $(shell printf '%s\n' '$(CXX)' '$(CXX_TOOLCHAIN_FLAGS)' '$(CPPFLAGS)' '$(CXXFLAGS_OPT)' '$(CXXFLAGS_PCH)' '$(shell $(CXX) --version 2>/dev/null | head -n 1)' '$(shell $(CXX) -dumpmachine 2>/dev/null)' | cksum | cut -d' ' -f1)
COMPILER_OBJ_DIR_DEV = $(COMPILER_DIR)/obj/dev-$(COMPILER_CONFIG_DEV)
COMPILER_OBJ_DIR_OPT = $(COMPILER_DIR)/obj/opt-$(COMPILER_CONFIG_OPT)
COMPILER_OBJECTS_DEV = $(patsubst %.cpp,$(COMPILER_OBJ_DIR_DEV)/%.o,$(COMPILER_SOURCES))
COMPILER_OBJECTS_OPT = $(patsubst %.cpp,$(COMPILER_OBJ_DIR_OPT)/%.o,$(COMPILER_SOURCES))
COMPILER_DEPFILES_DEV = $(COMPILER_OBJECTS_DEV:.o=.d)
COMPILER_DEPFILES_OPT = $(COMPILER_OBJECTS_OPT:.o=.d)
COMPILER_ACTIVE_CONFIG_DEV = $(MOSS_DEV).config
COMPILER_ACTIVE_CONFIG_OPT = $(MOSS_OPT).config
COMPILER_LINK_CONFIG_DEV = $(shell printf '%s\n' '$(COMPILER_CONFIG_DEV)' '$(LINKER)' '$(LDFLAGS)' '$(MOSS_LDFLAGS)' '$(LDLIBS)' '$(shell $(LINKER) --version 2>/dev/null | head -n 1)' '$(if $(findstring mold,$(MOSS_LDFLAGS)),$(MOLD_VERSION))' | cksum | cut -d' ' -f1)
COMPILER_LINK_CONFIG_OPT = $(shell printf '%s\n' '$(COMPILER_CONFIG_OPT)' '$(LINKER)' '$(LDFLAGS)' '$(MOSS_LDFLAGS)' '$(LDLIBS)' '$(shell $(LINKER) --version 2>/dev/null | head -n 1)' '$(if $(findstring mold,$(MOSS_LDFLAGS)),$(MOLD_VERSION))' | cksum | cut -d' ' -f1)

# Clang shares one stable standard-library PCH across compiler translation
# units. GCC remains an explicit compatibility path without Clang PCH flags.
CXXFLAGS_PCH ?=
COMPILER_PCH_DEV = $(COMPILER_OBJ_DIR_DEV)/compiler_pch.pch
COMPILER_PCH_OPT = $(COMPILER_OBJ_DIR_OPT)/compiler_pch.pch
ifneq ($(filter clang++ clang++-%,$(notdir $(firstword $(CXX)))),)
COMPILER_PCH_FLAGS_DEV = -include-pch $(COMPILER_PCH_DEV)
COMPILER_PCH_FLAGS_OPT = -include-pch $(COMPILER_PCH_OPT)
COMPILER_PCH_DEPFILES = $(COMPILER_PCH_DEV:.pch=.d) $(COMPILER_PCH_OPT:.pch=.d)
else
COMPILER_PCH_FLAGS_DEV =
COMPILER_PCH_FLAGS_OPT =
COMPILER_PCH_DEPFILES =
endif

LINKER ?= $(CXX)
# Link with mold through the compiler driver; override with MOSS_LDFLAGS=.
MOSS_LDFLAGS ?= -fuse-ld=mold
LDFLAGS ?=
LDLIBS ?=
MOLD_VERSION ?= $(shell mold --version 2>/dev/null | head -n 1)

MOSS ?= ./moss
RUSTC ?= rustc
EXAMPLE_BUILD_DIR ?= build/examples/optimized
TEST_JOBS ?= $(shell nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 16)

.PHONY: all moss moss-dev moss-opt moss-release check check-parallel check-serial check-perf compiler-perf package-perf c1-corpus-gates examples examples-optimized clean help FORCE

# Default target: build fast development compiler (-O0) and update ./moss
all: moss-dev

moss: moss-dev

moss-dev: $(MOSS_DEV)
	@cp -f $(MOSS_DEV) moss

# These active-link stamps detect toolchain/flag switches even though all
# configurations intentionally preserve the public executable paths.
FORCE:

$(COMPILER_ACTIVE_CONFIG_DEV): FORCE
	@if ! test -f $@ || test "$$(cat $@)" != "$(COMPILER_LINK_CONFIG_DEV)"; then \
		printf '%s\n' "$(COMPILER_LINK_CONFIG_DEV)" > $@.tmp; mv -f $@.tmp $@; \
	fi

$(COMPILER_ACTIVE_CONFIG_OPT): FORCE
	@if ! test -f $@ || test "$$(cat $@)" != "$(COMPILER_LINK_CONFIG_OPT)"; then \
		printf '%s\n' "$(COMPILER_LINK_CONFIG_OPT)" > $@.tmp; mv -f $@.tmp $@; \
	fi

$(MOSS_DEV): $(COMPILER_OBJECTS_DEV) $(COMPILER_ACTIVE_CONFIG_DEV)
	@mkdir -p $(@D)
	$(LINKER) $(CXX_TOOLCHAIN_FLAGS) $(CXXFLAGS_DEV) $(LDFLAGS) $(MOSS_LDFLAGS) $(COMPILER_OBJECTS_DEV) $(LDLIBS) -o $@.tmp
	@mv -f $@.tmp $@

moss-opt: $(MOSS_OPT)

moss-release: $(MOSS_RELEASE)

$(MOSS_RELEASE): $(MOSS_OPT)
	@cp -f $(MOSS_OPT) $@

$(MOSS_OPT): $(COMPILER_OBJECTS_OPT) $(COMPILER_ACTIVE_CONFIG_OPT)
	@mkdir -p $(@D)
	$(LINKER) $(CXX_TOOLCHAIN_FLAGS) $(CXXFLAGS_OPT) $(LDFLAGS) $(MOSS_LDFLAGS) $(COMPILER_OBJECTS_OPT) $(LDLIBS) -o $@.tmp
	@mv -f $@.tmp $@

ifneq ($(filter clang++ clang++-%,$(notdir $(firstword $(CXX)))),)
$(COMPILER_PCH_DEV): src/compiler_pch.hpp
	@mkdir -p $(@D)
	$(CXX) $(CXX_TOOLCHAIN_FLAGS) $(CPPFLAGS) $(CXXFLAGS_DEV) $(CXXFLAGS_PCH) -x c++-header -MMD -MP -MF $(@:.pch=.d) -MT $@ $< -o $@.tmp
	@mv -f $@.tmp $@
	@touch -r $@ $(@:.pch=.d)

$(COMPILER_PCH_OPT): src/compiler_pch.hpp
	@mkdir -p $(@D)
	$(CXX) $(CXX_TOOLCHAIN_FLAGS) $(CPPFLAGS) $(CXXFLAGS_OPT) $(CXXFLAGS_PCH) -x c++-header -MMD -MP -MF $(@:.pch=.d) -MT $@ $< -o $@.tmp
	@mv -f $@.tmp $@
	@touch -r $@ $(@:.pch=.d)

$(COMPILER_PCH_DEV): $(COMPILER_PCH_DEV:.pch=.d)
$(COMPILER_PCH_OPT): $(COMPILER_PCH_OPT:.pch=.d)
$(COMPILER_OBJECTS_DEV): $(COMPILER_PCH_DEV)
$(COMPILER_OBJECTS_OPT): $(COMPILER_PCH_OPT)
endif

$(COMPILER_OBJ_DIR_DEV)/%.o: %.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXX_TOOLCHAIN_FLAGS) $(CPPFLAGS) $(CXXFLAGS_DEV) $(CXXFLAGS_PCH) $(COMPILER_PCH_FLAGS_DEV) -MMD -MP -MF $(@:.o=.d) -MT $@ -c $< -o $@
	@touch -r $@ $(@:.o=.d)

$(COMPILER_OBJ_DIR_OPT)/%.o: %.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXX_TOOLCHAIN_FLAGS) $(CPPFLAGS) $(CXXFLAGS_OPT) $(CXXFLAGS_PCH) $(COMPILER_PCH_FLAGS_OPT) -MMD -MP -MF $(@:.o=.d) -MT $@ -c $< -o $@
	@touch -r $@ $(@:.o=.d)

# Missing depfiles make their corresponding objects out of date and are
# regenerated by the compile. -MP supplies empty rules for removed headers.
$(COMPILER_DEPFILES_DEV) $(COMPILER_DEPFILES_OPT): ;
$(COMPILER_PCH_DEPFILES): ;
$(foreach object,$(COMPILER_OBJECTS_DEV),$(eval $(object): $(object:.o=.d)))
$(foreach object,$(COMPILER_OBJECTS_OPT),$(eval $(object): $(object:.o=.d)))

-include $(COMPILER_DEPFILES_DEV) $(COMPILER_DEPFILES_OPT) $(COMPILER_PCH_DEPFILES)

check: check-parallel

check-parallel: moss-dev moss-opt
	python3 tests/tooling/check_phase27_string_debt.py
	python3 tests/tooling/check_phase27_est.py $(MOSS_DEV)
	python3 tests/run_parallel.py --jobs $(TEST_JOBS) --compiler-dev $(MOSS_DEV) --compiler-opt $(MOSS_OPT)

check-serial: moss
	sh tests/run.sh ./moss build/tests

check-perf: moss-opt
	python3 tests/tooling/check_phase25_4_performance_baseline.py $(MOSS_OPT)

compiler-perf: moss-opt
	python3 benchmarks/phase25_4/cpp_baseline/local_suite.py

package-perf: moss-opt
	python3 benchmarks/phase26_1/m1_suite.py

c1-corpus-gates: moss-dev
	python3 tests/tooling/check_phase26_1_c1_corpus_scope_gates.py $(MOSS_DEV)

bench-torture: moss-opt
	python3 benchmarks/phase25_4/cpp_baseline/run.py --moss $(MOSS_OPT)

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
	@echo "  make check-perf   - Run Phase 25.4 B1 permanent compiler performance regression gate"
	@echo "  make compiler-perf - Run compiler timing/profiling; MOSS_PERF_INCLUDE_REAL_WORLD=1 adds curated Moss workloads; MOSS_PERF_CERTIFY=1 enables certification"
	@echo "  make package-perf  - Run Phase 26.1 package edit/revert work-counter scenarios"
	@echo "  make c1-corpus-gates - Run Phase 26.1 G-a/G-b file/package scope gates"
	@echo "  make bench-torture - Run full portable compiler torture benchmark suite"
	@echo "  make examples     - Build and verify optimized examples"
	@echo "  make clean        - Remove build artifacts and binaries"
