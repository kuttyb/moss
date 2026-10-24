package org.mosslang.compiler.ast;

import org.mosslang.compiler.types.TypeContext;
import org.mosslang.compiler.types.TypeParser;
import org.mosslang.compiler.types.TypeRef;

public class Field {
    public String name = "";
    public String type = "";
    public String init = "";
    public String header = "";
    public int line = 0;
    public String sourceFile = "";
    public boolean inferred = false;
    public boolean exported = false;
    public TypeRef typeRef = null;

    public Field() {}

    public Field(String name, String type, String init, String header, int line, String sourceFile, boolean inferred, boolean exported) {
        this.name = name != null ? name : "";
        this.init = init != null ? init : "";
        this.header = header != null ? header : "";
        this.line = line;
        this.sourceFile = sourceFile != null ? sourceFile : "";
        this.inferred = inferred;
        this.exported = exported;
        setType(type);
    }

    public void bindType(TypeRef term) {
        this.typeRef = term;
        if (typeRef != null) {
            this.type = typeRef.toCanonicalString();
        } else {
            this.type = "";
        }
    }

    public void setType(String raw) {
        this.type = raw != null ? raw : "";
        if (!this.type.isEmpty() && TypeContext.current() != null) {
            TypeRef parsed = TypeParser.parseCanonical(this.type);
            if (parsed != null && parsed.isClosed()) {
                this.typeRef = parsed;
                return;
            }
        }
        this.typeRef = null;
    }

    public void sync() {
        if (typeRef != null) {
            this.type = typeRef.toCanonicalString();
        } else if (!type.isEmpty() && TypeContext.current() != null) {
            TypeRef parsed = TypeParser.parseCanonical(type);
            if (parsed != null && parsed.isClosed()) {
                this.typeRef = parsed;
            }
        }
    }

    public void assertConsistent() {
        if (typeRef != null && !type.equals(typeRef.toCanonicalString())) {
            throw new IllegalStateException("Field '" + name + "' type string projection '" +
                    type + "' contradicts bound TypeRef '" +
                    typeRef.toCanonicalString() + "'");
        }
    }

    @Override
    public String toString() {
        return "Field{name='" + name + "', type='" + type + "', line=" + line + "}";
    }
}
