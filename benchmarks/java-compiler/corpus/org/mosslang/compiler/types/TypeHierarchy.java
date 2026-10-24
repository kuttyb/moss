package org.mosslang.compiler.types;

import org.eclipse.collections.api.map.primitive.MutableIntObjectMap;
import org.eclipse.collections.api.set.primitive.MutableIntSet;
import org.eclipse.collections.impl.map.mutable.primitive.IntObjectHashMap;
import org.eclipse.collections.impl.set.mutable.primitive.IntHashSet;

public class TypeHierarchy {
    private final MutableIntObjectMap<MutableIntSet> directSubtypes = new IntObjectHashMap<>();
    private final MutableIntObjectMap<MutableIntSet> directSupertypes = new IntObjectHashMap<>();

    public void addSubtypeRelation(int subTypeId, int superTypeId) {
        directSubtypes.getIfAbsentPut(superTypeId, IntHashSet::new).add(subTypeId);
        directSupertypes.getIfAbsentPut(subTypeId, IntHashSet::new).add(superTypeId);
    }

    public boolean isAncestor(int candidateSuper, int candidateSub) {
        if (candidateSuper == candidateSub) return true;
        MutableIntSet supers = directSupertypes.get(candidateSub);
        if (supers == null || supers.isEmpty()) return false;
        if (supers.contains(candidateSuper)) return true;
        for (int p : supers.toArray()) {
            if (isAncestor(candidateSuper, p)) return true;
        }
        return false;
    }
}
