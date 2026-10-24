package org.mosslang.compiler.types;

import org.eclipse.collections.api.map.primitive.MutableIntObjectMap;
import org.eclipse.collections.impl.map.mutable.primitive.IntObjectHashMap;

public class TypeSubstitutionMap implements TypeSubstitution {
    private final MutableIntObjectMap<TypeRef> bindings = new IntObjectHashMap<>();

    @Override
    public TypeRef lookup(int varId) {
        TypeRef target = bindings.get(varId);
        if (target == null) return null;
        if (target instanceof TypeVar tv && bindings.containsKey(tv.id())) {
            return lookup(tv.id());
        }
        return target;
    }

    @Override
    public boolean contains(int varId) {
        return bindings.containsKey(varId);
    }

    @Override
    public void bind(int varId, TypeRef type) {
        bindings.put(varId, type);
    }

    public TypeRef apply(TypeRef type) {
        return type.substitute(this);
    }

    public int size() {
        return bindings.size();
    }
}
