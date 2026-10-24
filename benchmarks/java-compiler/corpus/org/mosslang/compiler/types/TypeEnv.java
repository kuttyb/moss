package org.mosslang.compiler.types;

import org.eclipse.collections.api.map.MutableMap;
import org.eclipse.collections.impl.map.mutable.UnifiedMap;

public class TypeEnv {
    private final TypeEnv parent;
    private final MutableMap<String, TypeRef> bindings = new UnifiedMap<>();

    public TypeEnv(TypeEnv parent) {
        this.parent = parent;
    }

    public void bind(String name, TypeRef type) {
        bindings.put(name, type);
    }

    public TypeRef lookup(String name) {
        TypeRef t = bindings.get(name);
        if (t != null) return t;
        return parent != null ? parent.lookup(name) : null;
    }

    public boolean isLocal(String name) {
        return bindings.containsKey(name);
    }
}
