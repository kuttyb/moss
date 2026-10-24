package org.mosslang.compiler.types;

import org.eclipse.collections.api.set.primitive.MutableIntSet;

public record DomainType(int typeId, String domainName) implements TypeRef {
    @Override
    public String name() { return "Domain[" + domainName + "]"; }
    @Override
    public boolean isPrimitive() { return false; }
    @Override
    public boolean isFunction() { return false; }
    @Override
    public boolean isDomain() { return true; }

    @Override
    public boolean isSubtypeOf(TypeRef other) {
        return this.equals(other);
    }

    @Override
    public TypeRef substitute(TypeSubstitution substitution) {
        return this;
    }

    @Override
    public void collectTypeVarIds(MutableIntSet accumulator) {}
}
