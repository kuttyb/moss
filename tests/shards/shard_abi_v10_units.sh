#!/bin/sh
set -eu

compiler=${1:-./moss}
test_build=${2:-build/tests/abi_v10_units}
mkdir -p "$test_build"

. "$(dirname "$0")/../lib_test.sh"

mkdir -p tmp
${CXX:-c++} -std=c++17 -Wall -Wextra -Werror -pedantic \
  tests/tooling/abi_v10_staging.cpp -o tmp/abi_v10_staging
tmp/abi_v10_staging
PYTHONDONTWRITEBYTECODE=1 python3 tests/tooling/check_phase25_known_open_nominal.py "$compiler"
${CXX:-c++} -std=c++17 -Wall -Wextra -Werror -pedantic -Isrc \
  tests/tooling/abi_v10_adversarial.cpp -o tmp/abi_v10_adversarial
tmp/abi_v10_adversarial
