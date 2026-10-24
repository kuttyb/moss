package org.mosslang.compiler.types;

import org.eclipse.collections.api.map.MutableMap;
import org.eclipse.collections.api.set.primitive.MutableIntSet;
import org.eclipse.collections.impl.map.mutable.UnifiedMap;

public record RecordType(int typeId, String typeName, MutableMap<String, TypeRef> fields) implements TypeRef {
    @Override
    public String name() { return typeName; }
    @Override
    public boolean isPrimitive() { return false; }
    @Override
    public boolean isFunction() { return false; }
    @Override
    public boolean isDomain() { return false; }

    @Override
    public boolean isSubtypeOf(TypeRef other) {
        if (other instanceof RecordType o) {
            if (!typeName.equals(o.typeName)) return false;
            for (String f : o.fields.keySet()) {
                TypeRef myField = fields.get(f);
                if (myField == null || !myField.isSubtypeOf(o.fields.get(f))) return false;
            }
            return true;
        }
        return false;
    }

    @Override
    public TypeRef substitute(TypeSubstitution substitution) {
        MutableMap<String, TypeRef> newFields = new UnifiedMap<>();
        fields.forEachKeyValue((k, v) -> newFields.put(k, v.substitute(substitution)));
        return new RecordType(typeId, typeName, newFields);
    }

    @Override
    public void collectTypeVarIds(MutableIntSet accumulator) {
        fields.forEachValue(v -> v.collectTypeVarIds(accumulator));
    }
}
