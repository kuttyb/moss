package org.mosslang.compiler.ast;

import org.mosslang.compiler.types.TypeContext;
import org.mosslang.compiler.types.TypeParser;
import org.mosslang.compiler.types.TypeRef;

public class Param {
    public String name = "";
    public String type = "";
    public boolean inferred = false;
    public TypeRef typeRef = null;

    public Param() {}

    public Param(String name, String type, boolean inferred) {
        this.name = name != null ? name : "";
        this.inferred = inferred;
        setType(type);
    }

    public Param(String name, String type) {
        this(name, type, false);
    }

    public Param(String name, TypeRef typeRef, boolean inferred) {
        this.name = name != null ? name : "";
        this.inferred = inferred;
        bindType(typeRef);
    }

    public Param(String name, TypeRef typeRef) {
        this(name, typeRef, false);
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
            throw new IllegalStateException("Param '" + name + "' type string projection '" +
                    type + "' contradicts bound TypeRef '" +
                    typeRef.toCanonicalString() + "'");
        }
    }

    @Override
    public String toString() {
        return "Param{name='" + name + "', type='" + type + "'}";
    }
}
