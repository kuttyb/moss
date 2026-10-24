#!/usr/bin/env bash
set -euo pipefail

# MOSS PHASE 25.3 J0: Top-Level Reproducibility Script
# Executes representative workload generation, compilation verification across all 3 toolchains,
# fixture test execution, and benchmark measurement suite.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

echo "================================================================================"
echo "MOSS PHASE 25.3 J0: JAVA COMPILER BUILD-SYSTEM SHOOTOUT"
echo "================================================================================"

# 1. Environment & Prerequisite Check
echo ">>> Checking Environment & Installed Tools..."
command -v java >/dev/null 2>&1 || { echo "Error: java not found on PATH" >&2; exit 1; }
command -v javac >/dev/null 2>&1 || { echo "Error: javac not found on PATH" >&2; exit 1; }
command -v mill >/dev/null 2>&1 || { echo "Error: mill not found on PATH" >&2; exit 1; }
command -v python3 >/dev/null 2>&1 || { echo "Error: python3 not found on PATH" >&2; exit 1; }

if [ ! -d "/opt/eclipse-jdt" ]; then
    echo "Error: Eclipse JDT installation not found at /opt/eclipse-jdt" >&2
    exit 1
fi

echo "  Java version:  $(java -version 2>&1 | head -n 1)"
echo "  Javac version: $(javac -version 2>&1 | head -n 1)"
echo "  Mill version:  $(mill --version 2>&1 | head -n 1)"
echo "  Eclipse JDT:   /opt/eclipse-jdt (Equinox Launcher)"

# 2. Dependency check
echo ">>> Verifying Dependencies..."
if [ ! -f "${SCRIPT_DIR}/lib/eclipse-collections-13.0.0.jar" ] || [ ! -f "${SCRIPT_DIR}/lib/eclipse-collections-api-13.0.0.jar" ]; then
    echo "  Downloading Eclipse Collections 13.0.0..."
    mkdir -p "${SCRIPT_DIR}/lib"
    curl -sSfL "https://repo1.maven.org/maven2/org/eclipse/collections/eclipse-collections/13.0.0/eclipse-collections-13.0.0.jar" -o "${SCRIPT_DIR}/lib/eclipse-collections-13.0.0.jar"
    curl -sSfL "https://repo1.maven.org/maven2/org/eclipse/collections/eclipse-collections-api/13.0.0/eclipse-collections-api-13.0.0.jar" -o "${SCRIPT_DIR}/lib/eclipse-collections-api-13.0.0.jar"
fi
echo "  Dependencies verified."

# 3. Workload Verification & Manifest Check
echo ">>> Generating / Verifying Synthetic Corpus (Seed 42, ~200 Files)..."
python3 "${SCRIPT_DIR}/generator/generate_corpus.py" --target-files 200 --seed 42 --output-dir "${SCRIPT_DIR}/corpus"

echo ">>> Verifying SHA-256 Manifest..."
(cd "${SCRIPT_DIR}/corpus" && sha256sum -c manifest.sha256 >/dev/null)
echo "  SHA-256 manifest: 100% verified."

# 4. Correctness & Fixture Tests on All Toolchains
echo ">>> Compiling and Running Fixture Tests Across All Toolchains..."
mkdir -p "${SCRIPT_DIR}/javac/bin" "${SCRIPT_DIR}/jdt/bin" "${SCRIPT_DIR}/jdt/workspace"

# Toolchain A: Direct Javac
find "${SCRIPT_DIR}/corpus" -name "*.java" | sed 's/^/"/;s/$/"/' > "${SCRIPT_DIR}/javac/sources.txt"
javac --release 25 -sourcepath "" -cp "${SCRIPT_DIR}/lib/*:${SCRIPT_DIR}/javac/bin" -d "${SCRIPT_DIR}/javac/bin" @"${SCRIPT_DIR}/javac/sources.txt"
javac --release 25 -sourcepath "" -cp "${SCRIPT_DIR}/lib/*:${SCRIPT_DIR}/javac/bin" -d "${SCRIPT_DIR}/javac/bin" "${SCRIPT_DIR}/fixtures/CompilerFixtureTest.java"
java -cp "${SCRIPT_DIR}/lib/*:${SCRIPT_DIR}/javac/bin" org.mosslang.compiler.fixtures.CompilerFixtureTest >/dev/null
echo "  Toolchain A (Direct Javac): Fixture tests PASSED."

# Toolchain B: Mill + Zinc
(cd "${SCRIPT_DIR}/mill" && mill compiler.compile >/dev/null)
javac --release 25 -sourcepath "" -cp "${SCRIPT_DIR}/lib/*:${SCRIPT_DIR}/mill/out/compiler/compile.dest/classes" -d "${SCRIPT_DIR}/mill/out/compiler/compile.dest/classes" "${SCRIPT_DIR}/fixtures/CompilerFixtureTest.java"
java -cp "${SCRIPT_DIR}/lib/*:${SCRIPT_DIR}/mill/out/compiler/compile.dest/classes" org.mosslang.compiler.fixtures.CompilerFixtureTest >/dev/null
echo "  Toolchain B (Mill + Zinc):  Fixture tests PASSED."

# Toolchain C: Eclipse JDT Headless
# Test headless batch build
/opt/eclipse-jdt/eclipse -nosplash -data "${SCRIPT_DIR}/jdt/workspace" \
    -configuration "${SCRIPT_DIR}/jdt/configuration" \
    -application org.mosslang.jdt.builder.headlessBuilder \
    -action init -src "${SCRIPT_DIR}/corpus" -lib "${SCRIPT_DIR}/lib" -bin "${SCRIPT_DIR}/jdt/bin" >/dev/null 2>&1
/opt/eclipse-jdt/eclipse -nosplash -data "${SCRIPT_DIR}/jdt/workspace" \
    -configuration "${SCRIPT_DIR}/jdt/configuration" \
    -application org.mosslang.jdt.builder.headlessBuilder \
    -action clean_build -src "${SCRIPT_DIR}/corpus" -lib "${SCRIPT_DIR}/lib" -bin "${SCRIPT_DIR}/jdt/bin" >/dev/null 2>&1
javac --release 25 -sourcepath "" -cp "${SCRIPT_DIR}/lib/*:${SCRIPT_DIR}/jdt/bin" -d "${SCRIPT_DIR}/jdt/bin" "${SCRIPT_DIR}/fixtures/CompilerFixtureTest.java"
java -cp "${SCRIPT_DIR}/lib/*:${SCRIPT_DIR}/jdt/bin" org.mosslang.compiler.fixtures.CompilerFixtureTest >/dev/null
echo "  Toolchain C (Eclipse JDT):  Fixture tests PASSED."

# Pre-compile MutationVerifier
mkdir -p "${SCRIPT_DIR}/fixtures/bin"
javac --release 25 -sourcepath "" -cp "${SCRIPT_DIR}/corpus:${SCRIPT_DIR}/lib/*" -d "${SCRIPT_DIR}/fixtures/bin" "${SCRIPT_DIR}/fixtures/MutationVerifier.java"

# 5. Benchmark Matrix Execution
TRIALS="${1:-15}"
echo ">>> Running Benchmark Matrix (${TRIALS} trials per scenario)..."
python3 "${SCRIPT_DIR}/scripts/run_benchmarks.py" "${TRIALS}"

# 6. Summary Display
echo ">>> Summary Table:"
cat "${SCRIPT_DIR}/results/summary_table.csv"
echo ""
echo "Shootout execution completed successfully."
