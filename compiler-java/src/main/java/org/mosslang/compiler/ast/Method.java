package org.mosslang.compiler.ast;

import org.eclipse.collections.api.list.MutableList;
import org.eclipse.collections.impl.list.mutable.FastList;
import org.eclipse.collections.impl.list.mutable.primitive.IntArrayList;
import org.mosslang.compiler.types.TypeContext;
import org.mosslang.compiler.types.TypeParser;
import org.mosslang.compiler.types.TypeRef;
import org.mosslang.compiler.types.AccessMode;

public class Method {
    public String owner = "";
    public String name = "";
    public MutableList<Param> params = new FastList<>();
    public String returnType = null;
    public TypeRef returnTypeRef = null;
    public MutableList<Stmt> body = new FastList<>();
    public String resultExpression = null;
    public int resultLine = 0;
    public IntArrayList resultContinuationLines = new IntArrayList();
    public int line = 0;
    public String sourceFile = "";
    public AccessMode receiverAccessMode = AccessMode.Read;
    public java.util.List<AccessMode> parameterAccessModes = new java.util.ArrayList<>();
    public boolean sourceFree = false;
    public java.util.List<String> sourceFreeRaiseVariants = new java.util.ArrayList<>();
    public boolean sourceFreeMayPanic = false;

    public Method() {}

    public Method(String owner, String name, int line, String sourceFile) {
        this.owner = owner != null ? owner : "";
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
            throw new IllegalStateException("Method '" + owner + "." + name +
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
        return "Method{" + owner + "." + name + " line=" + line + "}";
    }
}
