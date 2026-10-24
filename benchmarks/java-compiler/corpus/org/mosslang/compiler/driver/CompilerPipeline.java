package org.mosslang.compiler.driver;

public interface CompilerPipeline {
    String name();
    boolean execute(CompilerContext context);
}
