package org.mosslang.compiler.types;

import org.eclipse.collections.api.set.primitive.MutableIntSet;

public record PrimitiveType(PrimitiveKind kind) implements TypeRef {
    public static final PrimitiveType INT = new PrimitiveType(PrimitiveKind.INT);
    public static final PrimitiveType FLOAT = new PrimitiveType(PrimitiveKind.FLOAT);
    public static final PrimitiveType BOOL = new PrimitiveType(PrimitiveKind.BOOL);
    public static final PrimitiveType STRING = new PrimitiveType(PrimitiveKind.STRING);
    public static final PrimitiveType CHAR = new PrimitiveType(PrimitiveKind.CHAR);
    public static final PrimitiveType UNIT = new PrimitiveType(PrimitiveKind.UNIT);
    public static final PrimitiveType NEVER = new PrimitiveType(PrimitiveKind.NEVER);

    @Override
    public int typeId() { return kind.getId(); }
    @Override
    public String name() { return kind.getName(); }
    @Override
    public boolean isPrimitive() { return true; }
    @Override
    public boolean isFunction() { return false; }
    @Override
    public boolean isDomain() { return false; }

    @Override
    public boolean isSubtypeOf(TypeRef other) {
        if (this.equals(other)) return true;
        if (this.kind == PrimitiveKind.NEVER) return true;
        return false;
    }

    @Override
    public TypeRef substitute(TypeSubstitution substitution) {
        return this;
    }

    @Override
    public void collectTypeVarIds(MutableIntSet accumulator) {}
}
