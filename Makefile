CXX ?= c++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -pedantic

.PHONY: all check examples examples-optimized clean

MOSS ?= ./moss
RUSTC ?= rustc
EXAMPLE_BUILD_DIR ?= build/examples/optimized

all: moss

moss: src/moss.cpp
	$(CXX) $(CXXFLAGS) $< -o $@

check: moss
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
	rm -rf build/tests build/examples
