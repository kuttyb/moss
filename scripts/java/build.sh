#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
COMPILER_JAVA="${REPO_ROOT}/compiler-java"
SRC_DIR="${COMPILER_JAVA}/src/main/java"
BIN_DIR="${COMPILER_JAVA}/bin"
LIB_DIR="${COMPILER_JAVA}/lib"

CLEAN=0
SUBSYSTEM="all"
for arg in "$@"; do
    if [ "$arg" == "--clean" ]; then
        CLEAN=1
    elif [[ "$arg" == --subsystem=* ]]; then
        SUBSYSTEM="${arg#--subsystem=}"
    fi
done

BUILD_MODE="full-javac"
if [ "$CLEAN" -eq 1 ]; then
    rm -rf "${BIN_DIR}"
    BUILD_MODE="clean-javac"
fi
mkdir -p "${BIN_DIR}"

CP="${LIB_DIR}/eclipse-collections-13.0.0.jar:${LIB_DIR}/eclipse-collections-api-13.0.0.jar"

SOURCES=$(find "${SRC_DIR}" -name "*.java")
if [ -z "${SOURCES}" ]; then
    echo "No Java sources found in ${SRC_DIR}"
    exit 1
fi

T0=$(date +%s%3N)
javac -d "${BIN_DIR}" -cp "${CP}" ${SOURCES}
if [ -d "${COMPILER_JAVA}/src/main/resources" ]; then
    cp -r "${COMPILER_JAVA}/src/main/resources/." "${BIN_DIR}/"
fi
T1=$(date +%s%3N)
ELAPSED_MS=$((T1 - T0))

mkdir -p "${REPO_ROOT}/tmp"
printf '{"timestamp":"%s","builder":"javac","mode":"%s","subsystem":"%s","elapsed_ms":%d}\n' \
    "$(date -Iseconds)" "${BUILD_MODE}" "${SUBSYSTEM}" "${ELAPSED_MS}" >> "${REPO_ROOT}/tmp/build_times.jsonl"

echo "Java build successful (${BUILD_MODE}, subsystem=${SUBSYSTEM}, ${ELAPSED_MS}ms) -> ${BIN_DIR}"
