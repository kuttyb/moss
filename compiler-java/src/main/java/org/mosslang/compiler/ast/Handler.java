package org.mosslang.compiler.ast;

import org.eclipse.collections.api.list.MutableList;
import org.eclipse.collections.impl.list.mutable.FastList;

public class Handler {
    public String name = "";
    public String header = "";
    public MutableList<Param> params = new FastList<>();
    public String replyType = null;
    public MutableList<Stmt> body = new FastList<>();
    public MutableList<FailureArm> failureArms = new FastList<>();
    public int line = 0;
    public String sourceFile = "";

    public Handler() {}

    public Handler(String name, String header, int line, String sourceFile) {
        this.name = name != null ? name : "";
        this.header = header != null ? header : "";
        this.line = line;
        this.sourceFile = sourceFile != null ? sourceFile : "";
    }

    public void setReplyType(String replyType) {
        this.replyType = replyType;
    }

    @Override
    public String toString() {
        return "Handler{" + name + " line=" + line + "}";
    }
}
