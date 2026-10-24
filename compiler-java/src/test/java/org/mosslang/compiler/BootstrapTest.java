package org.mosslang.compiler;

import org.mosslang.compiler.cli.Main;

public class BootstrapTest {
    public static void main(String[] args) {
        System.out.println("Running BootstrapTest...");
        int res = Main.run(new String[]{"--version"});
        if (res != 0) throw new AssertionError("Main --version returned " + res);
        int helpRes = Main.run(new String[]{"--help"});
        if (helpRes != 0) throw new AssertionError("Main --help returned " + helpRes);
        System.out.println("BootstrapTest PASSED.");
    }
}
