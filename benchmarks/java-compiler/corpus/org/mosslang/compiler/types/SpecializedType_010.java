package org.mosslang.compiler.types;

import org.eclipse.collections.api.set.primitive.MutableIntSet;

public record SpecializedType_010(int typeId, String name, TypeRef parentType) implements TypeRef {
    @Override
    public boolean isPrimitive() { return false; }
    @Override
    public boolean isFunction() { return false; }
    @Override
    public boolean isDomain() { return false; }

    @Override
    public boolean isSubtypeOf(TypeRef other) {
        if (this.equals(other)) return true;
        return parentType != null && parentType.isSubtypeOf(other);
    }

    @Override
    public TypeRef substitute(TypeSubstitution substitution) {
        TypeRef newParent = parentType != null ? parentType.substitute(substitution) : null;
        return new SpecializedType_010(typeId, name, newParent);
    }

    @Override
    public void collectTypeVarIds(MutableIntSet accumulator) {
        if (parentType != null) parentType.collectTypeVarIds(accumulator);
    }
}
