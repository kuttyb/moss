package org.mosslang.compiler.cli;

public class Main {
    public static final String COMPILER_VERSION = "0.1.0-java";
    public static final String LANGUAGE_VERSION = "moss-0.1";

    public static void main(String[] args) {
        System.exit(run(args));
    }

    public static int run(String[] args) {
        if (args.length == 0) {
            usage();
            return 2;
        }
        String first = args[0];
        if ("--version".equals(first) || "-v".equals(first)) {
            System.out.println("Moss Java Compiler " + COMPILER_VERSION + " (" + LANGUAGE_VERSION + ")");
            return 0;
        }
        if ("--help".equals(first) || "-h".equals(first)) {
            usage();
            return 0;
        }
        if ("agent".equals(first) && args.length > 1 && "bootstrap".equals(args[1])) {
            System.out.println("{\"result\":{\"compiler\":\"moss-java\",\"language_version\":\"" + LANGUAGE_VERSION + "\"}}");
            return 0;
        }
        // Placeholder for further commands
        return 0;
    }

    public static void usage() {
        System.err.println("usage: moss <source.moss> [-o output.rs] [--check] [--json]");
        System.err.println("       moss run --interp <source.moss> [--trace]");
        System.err.println("       moss check [--json] <source.moss>");
    }
}
