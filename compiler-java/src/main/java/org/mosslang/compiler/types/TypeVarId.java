package org.mosslang.compiler.types;

import java.util.Objects;

public record TypeVarId(int sessionId, int ownerId, int ordinal) implements Comparable<TypeVarId> {
    @Override
    public int compareTo(TypeVarId o) {
        if (this.sessionId != o.sessionId) {
            return Integer.compare(this.sessionId, o.sessionId);
        }
        if (this.ownerId != o.ownerId) {
            return Integer.compare(this.ownerId, o.ownerId);
        }
        return Integer.compare(this.ordinal, o.ordinal);
    }

    @Override
    public String toString() {
        return "v" + sessionId + ":" + ownerId + ":" + ordinal;
    }
}
