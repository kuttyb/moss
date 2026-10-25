package org.mosslang.compiler.ast;

import org.eclipse.collections.api.list.MutableList;
import org.eclipse.collections.impl.list.mutable.FastList;
import org.mosslang.compiler.types.TypeContext;
import org.mosslang.compiler.types.TypeParser;
import org.mosslang.compiler.types.TypeRef;

public class Handler {
    public String name = "";
    public String header = "";
    public MutableList<Param> params = new FastList<>();
    public String replyType = null;
    public TypeRef replyTypeRef = null;
    public MutableList<Stmt> body = new FastList<>();
    public MutableList<FailureArm> failureArms = new FastList<>();
    public int line = 0;
    public String sourceFile = "";
    public boolean sourceFreeMayRaise = false;

    public Handler() {}

    public Handler(String name, String header, int line, String sourceFile) {
        this.name = name != null ? name : "";
        this.header = header != null ? header : "";
        this.line = line;
        this.sourceFile = sourceFile != null ? sourceFile : "";
    }

    public void bindReplyType(TypeRef term) {
        this.replyTypeRef = term;
        if (replyTypeRef != null) {
            this.replyType = replyTypeRef.toCanonicalString();
        } else {
            this.replyType = null;
        }
    }

    public void setReplyType(String raw) {
        this.replyType = raw;
        if (replyType != null && !replyType.isEmpty() && TypeContext.current() != null) {
            TypeRef parsed = TypeParser.parseCanonical(replyType);
            if (parsed != null && parsed.isClosed()) {
                this.replyTypeRef = parsed;
                return;
            }
        }
        this.replyTypeRef = null;
    }

    public void sync() {
        if (replyTypeRef != null) {
            this.replyType = replyTypeRef.toCanonicalString();
        } else if (replyType != null && !replyType.isEmpty() && TypeContext.current() != null) {
            TypeRef parsed = TypeParser.parseCanonical(replyType);
            if (parsed != null && parsed.isClosed()) {
                this.replyTypeRef = parsed;
            }
        }
    }

    public void assertConsistent() {
        if (replyTypeRef != null &&
                (replyType == null || !replyType.equals(replyTypeRef.toCanonicalString()))) {
            throw new IllegalStateException("Handler '" + name +
                    "' reply type string projection '" + (replyType != null ? replyType : "<missing>") +
                    "' contradicts bound TypeRef '" +
                    replyTypeRef.toCanonicalString() + "'");
        }
        for (Param param : params) {
            param.assertConsistent();
        }
    }

    @Override
    public String toString() {
        return "Handler{" + name + " line=" + line + "}";
    }
}
