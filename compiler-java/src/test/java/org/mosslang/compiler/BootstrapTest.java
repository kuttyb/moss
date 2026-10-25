package org.mosslang.compiler;

import org.mosslang.compiler.cli.Main;

import java.io.ByteArrayOutputStream;
import java.io.PrintStream;
import java.nio.charset.StandardCharsets;

public class BootstrapTest {
    public static void main(String[] args) {
        System.out.println("Running BootstrapTest...");
        int res = Main.run(new String[]{"--version"});
        if (res != 0) throw new AssertionError("Main --version returned " + res);
        int helpRes = Main.run(new String[]{"--help"});
        if (helpRes != 0) throw new AssertionError("Main --help returned " + helpRes);
        PrintStream oldErr = System.err;
        ByteArrayOutputStream stderr = new ByteArrayOutputStream();
        try (PrintStream captured = new PrintStream(stderr, true, StandardCharsets.UTF_8)) {
            System.setErr(captured);
            int runJson = Main.run(new String[]{"run", "--interp", "--json", "tests/phase26_message_payload.moss"});
            if (runJson != 1) throw new AssertionError("run --json returned " + runJson);
        } finally {
            System.setErr(oldErr);
        }
        if (!"moss: run does not yet support --json\n".equals(stderr.toString(StandardCharsets.UTF_8)))
            throw new AssertionError("run --json rejection differed: " + stderr);
        System.out.println("BootstrapTest PASSED.");
    }
}
