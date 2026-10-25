#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
COMPILER_JAVA="${REPO_ROOT}/compiler-java"
STAGING="${REPO_ROOT}/tmp/java-moss-package"
MANIFEST="${REPO_ROOT}/tmp/java-moss-manifest.mf"
DIST="${COMPILER_JAVA}/dist"
JAR_PATH="${DIST}/moss.jar"

"${SCRIPT_DIR}/build.sh"
rm -rf "${STAGING}"
mkdir -p "${STAGING}" "${DIST}"
cp -R "${COMPILER_JAVA}/bin/." "${STAGING}/"
for DEPENDENCY in \
    "${COMPILER_JAVA}/lib/eclipse-collections-13.0.0.jar" \
    "${COMPILER_JAVA}/lib/eclipse-collections-api-13.0.0.jar"; do
    (cd "${STAGING}" && jar xf "${DEPENDENCY}")
done
rm -f "${STAGING}/META-INF/MANIFEST.MF"
find "${STAGING}/META-INF" -type f \( -name '*.SF' -o -name '*.RSA' -o -name '*.DSA' -o -name 'INDEX.LIST' \) -delete 2>/dev/null || true

cat > "${MANIFEST}" <<'EOF'
Manifest-Version: 1.0
Main-Class: org.mosslang.compiler.cli.Main
Implementation-Title: Moss Java Compiler
Implementation-Version: 0.1.0-java

EOF
jar cfm "${JAR_PATH}" "${MANIFEST}" -C "${STAGING}" .
cat > "${DIST}/moss" <<'EOF'
#!/usr/bin/env sh
SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
exec java -jar "${SCRIPT_DIR}/moss.jar" "$@"
EOF
chmod +x "${DIST}/moss"
echo "Packaged standalone compiler: ${JAR_PATH}"
