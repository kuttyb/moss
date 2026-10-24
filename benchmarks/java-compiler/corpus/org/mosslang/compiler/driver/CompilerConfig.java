package org.mosslang.compiler.driver;

public record CompilerConfig(
    boolean optimize,
    int optimizationLevel,
    boolean verifyAbi,
    boolean dumpIr,
    String targetPlatform
) {
    public static CompilerConfig defaultDebug() {
        return new CompilerConfig(false, 0, true, false, "native-x86_64");
    }

    public static CompilerConfig defaultRelease() {
        return new CompilerConfig(true, 2, true, false, "native-x86_64");
    }
}
