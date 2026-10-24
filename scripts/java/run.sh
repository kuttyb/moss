#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
COMPILER_JAVA="${REPO_ROOT}/compiler-java"
BIN_DIR="${COMPILER_JAVA}/bin"
LIB_DIR="${COMPILER_JAVA}/lib"

CP="${BIN_DIR}:${LIB_DIR}/eclipse-collections-13.0.0.jar:${LIB_DIR}/eclipse-collections-api-13.0.0.jar"

exec java -cp "${CP}" org.mosslang.compiler.cli.Main "$@"
