#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
COMPILER_JAVA="${REPO_ROOT}/compiler-java"
SRC_DIR="${COMPILER_JAVA}/src/main/java"
BIN_DIR="${COMPILER_JAVA}/bin"
LIB_DIR="${COMPILER_JAVA}/lib"

CLEAN=0
for arg in "$@"; do
    if [ "$arg" == "--clean" ]; then
        CLEAN=1
    fi
done

if [ "$CLEAN" -eq 1 ]; then
    rm -rf "${BIN_DIR}"
fi
mkdir -p "${BIN_DIR}"

CP="${LIB_DIR}/eclipse-collections-13.0.0.jar:${LIB_DIR}/eclipse-collections-api-13.0.0.jar"

SOURCES=$(find "${SRC_DIR}" -name "*.java")
if [ -z "${SOURCES}" ]; then
    echo "No Java sources found in ${SRC_DIR}"
    exit 1
fi

javac -d "${BIN_DIR}" -cp "${CP}" ${SOURCES}
echo "Java build successful -> ${BIN_DIR}"
