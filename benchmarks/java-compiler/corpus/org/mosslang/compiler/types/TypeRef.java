package org.mosslang.compiler.types;

import org.eclipse.collections.api.set.primitive.MutableIntSet;

public interface TypeRef {
    int typeId();
    String name();
    boolean isPrimitive();
    boolean isFunction();
    boolean isDomain();
    boolean isSubtypeOf(TypeRef other);
    TypeRef substitute(TypeSubstitution substitution);
    void collectTypeVarIds(MutableIntSet accumulator);

    default boolean isTypeVar() {
        return false;
    }
}
