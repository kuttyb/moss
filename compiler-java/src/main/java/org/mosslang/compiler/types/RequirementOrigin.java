package org.mosslang.compiler.types;

public class RequirementOrigin {
    public String moduleId = "";
    public String sourceFile = "";
    public int line = 0;
    public int column = 0;
    public String enclosingCallable = "";
    public String callExpressionId = "";

    public RequirementOrigin() {}

    public RequirementOrigin(String moduleId, String sourceFile, int line, int column,
                             String enclosingCallable, String callExpressionId) {
        this.moduleId = moduleId;
        this.sourceFile = sourceFile;
        this.line = line;
        this.column = column;
        this.enclosingCallable = enclosingCallable;
        this.callExpressionId = callExpressionId;
    }
}
