package org.mosslang.compiler.types;

import org.eclipse.collections.api.set.primitive.MutableIntSet;

public record TypeVar(int id, String varName) implements TypeRef {
    @Override
    public int typeId() { return 1000 + id; }
    @Override
    public String name() { return "'" + varName; }
    @Override
    public boolean isPrimitive() { return false; }
    @Override
    public boolean isFunction() { return false; }
    @Override
    public boolean isDomain() { return false; }
    @Override
    public boolean isTypeVar() { return true; }

    @Override
    public boolean isSubtypeOf(TypeRef other) {
        if (other instanceof TypeVar tv) {
            return this.id == tv.id;
        }
        return false;
    }

    @Override
    public TypeRef substitute(TypeSubstitution substitution) {
        TypeRef res = substitution.lookup(this.id);
        return res != null ? res : this;
    }

    @Override
    public void collectTypeVarIds(MutableIntSet accumulator) {
        accumulator.add(id);
    }
}
