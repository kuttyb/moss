package org.mosslang.compiler.types;

import org.eclipse.collections.api.set.primitive.MutableIntSet;

public record ArrayType(int typeId, TypeRef elementType) implements TypeRef {
    @Override
    public String name() { return "[" + elementType.name() + "]"; }
    @Override
    public boolean isPrimitive() { return false; }
    @Override
    public boolean isFunction() { return false; }
    @Override
    public boolean isDomain() { return false; }

    @Override
    public boolean isSubtypeOf(TypeRef other) {
        if (other instanceof ArrayType o) {
            return elementType.isSubtypeOf(o.elementType);
        }
        return false;
    }

    @Override
    public TypeRef substitute(TypeSubstitution substitution) {
        return new ArrayType(typeId, elementType.substitute(substitution));
    }

    @Override
    public void collectTypeVarIds(MutableIntSet accumulator) {
        elementType.collectTypeVarIds(accumulator);
    }
}
