package org.mosslang.compiler.ast;

import org.eclipse.collections.api.list.MutableList;
import org.eclipse.collections.impl.list.mutable.FastList;
import org.mosslang.compiler.types.TypeContext;
import org.mosslang.compiler.types.TypeParser;
import org.mosslang.compiler.types.TypeRef;

public class TraitMethod {
    public String name = "";
    public MutableList<Param> params = new FastList<>();
    public String returnType = null;
    public TypeRef returnTypeRef = null;
    public int line = 0;
    public String sourceFile = "";

    public TraitMethod() {}

    public TraitMethod(String name, int line, String sourceFile) {
        this.name = name != null ? name : "";
        this.line = line;
        this.sourceFile = sourceFile != null ? sourceFile : "";
    }

    public void bindReturnType(TypeRef term) {
        this.returnTypeRef = term;
        if (returnTypeRef != null) {
            this.returnType = returnTypeRef.toCanonicalString();
        } else {
            this.returnType = null;
        }
    }

    public void setReturnType(String raw) {
        this.returnType = raw;
        if (returnType != null && !returnType.isEmpty() && TypeContext.current() != null) {
            TypeRef parsed = TypeParser.parseCanonical(returnType);
            if (parsed != null && parsed.isClosed()) {
                this.returnTypeRef = parsed;
                return;
            }
        }
        this.returnTypeRef = null;
    }

    public void sync() {
        if (returnTypeRef != null) {
            this.returnType = returnTypeRef.toCanonicalString();
        } else if (returnType != null && !returnType.isEmpty() && TypeContext.current() != null) {
            TypeRef parsed = TypeParser.parseCanonical(returnType);
            if (parsed != null && parsed.isClosed()) {
                this.returnTypeRef = parsed;
            }
        }
    }

    public void assertConsistent() {
        if (returnTypeRef != null &&
                (returnType == null || !returnType.equals(returnTypeRef.toCanonicalString()))) {
            throw new IllegalStateException("TraitMethod '" + name +
                    "' return type string projection '" + (returnType != null ? returnType : "<missing>") +
                    "' contradicts bound TypeRef '" +
                    returnTypeRef.toCanonicalString() + "'");
        }
        for (Param param : params) {
            param.assertConsistent();
        }
    }

    @Override
    public String toString() {
        return "TraitMethod{" + name + " line=" + line + "}";
    }
}
