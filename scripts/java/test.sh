#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
COMPILER_JAVA="${REPO_ROOT}/compiler-java"
TEST_SRC="${COMPILER_JAVA}/src/test/java"
BIN_DIR="${COMPILER_JAVA}/bin"
LIB_DIR="${COMPILER_JAVA}/lib"

CP="${BIN_DIR}:${LIB_DIR}/eclipse-collections-13.0.0.jar:${LIB_DIR}/eclipse-collections-api-13.0.0.jar"

# First ensure compiler classes are built
if [ ! -d "${BIN_DIR}/org/mosslang/compiler" ]; then
    "${SCRIPT_DIR}/build.sh"
fi

TEST_SOURCES=$(find "${TEST_SRC}" -name "*Test.java" 2>/dev/null || true)
if [ -z "${TEST_SOURCES}" ]; then
    echo "No test sources found in ${TEST_SRC}"
    exit 0
fi

# Compile test sources into bin
javac -d "${BIN_DIR}" -cp "${CP}" ${TEST_SOURCES}

# Run each test
for src in ${TEST_SOURCES}; do
    # Determine FQCN
    rel="${src#"${TEST_SRC}/"}"
    fqcn="${rel%.java}"
    fqcn="${fqcn//\//.}"
    echo "=== Running ${fqcn} ==="
    java -ea -cp "${CP}" "${fqcn}"
done

echo "All Java unit tests passed."
