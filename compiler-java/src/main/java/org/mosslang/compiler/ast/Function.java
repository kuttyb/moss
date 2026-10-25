package org.mosslang.compiler.ast;

import org.eclipse.collections.api.list.MutableList;
import org.eclipse.collections.impl.list.mutable.FastList;
import org.eclipse.collections.impl.list.mutable.primitive.IntArrayList;
import org.mosslang.compiler.types.TypeContext;
import org.mosslang.compiler.types.AccessMode;
import org.mosslang.compiler.types.TypeParser;
import org.mosslang.compiler.types.TypeRef;

public class Function {
    public String name = "";
    public String header = "";
    public MutableList<Param> params = new FastList<>();
    public String returnType = null;
    public TypeRef returnTypeRef = null;
    public MutableList<Stmt> body = new FastList<>();
    public String definingModule = "";
    public String declarationName = "";
    public String semanticCallableId = "";
    public String resultExpression = null;
    public int resultLine = 0;
    public boolean expressionBody = false;
    public IntArrayList resultContinuationLines = new IntArrayList();
    public boolean generic = false;
    public boolean staticDispatch = false;
    public int sourceFreeAbiVersion = 0;
    public boolean sourceFreeCheckedBodyAvailable = false;
    public java.util.List<String> sourceFreeRaiseVariants = new java.util.ArrayList<>();
    public java.util.List<AccessMode> parameterAccessModes = new java.util.ArrayList<>();
    public java.util.List<FunctionSpecialization> specializations = new java.util.ArrayList<>();
    public int line = 0;
    public boolean exported = false;
    public String sourceFile = "";
    public String providerOriginFile = "";

    public Function() {}

    public Function(String name, String header, int line, String sourceFile, boolean exported) {
        this.name = name != null ? name : "";
        this.header = header != null ? header : "";
        this.line = line;
        this.sourceFile = sourceFile != null ? sourceFile : "";
        this.exported = exported;
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
            throw new IllegalStateException("Function '" + name +
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
        return "Function{" + name + " line=" + line + "}";
    }
}
