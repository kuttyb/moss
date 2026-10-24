package org.mosslang.compiler.types;

import java.util.Objects;

public class CallableId implements Comparable<CallableId> {
    private String moduleId = "";
    private String kind = "";
    private String declarationName = "";
    private String canonicalId = "";

    public CallableId() {}

    public CallableId(String id) {
        this.canonicalId = id != null ? id : "";
        parseId();
    }

    public CallableId(String moduleId, String kind, String declarationName) {
        this.moduleId = moduleId != null ? moduleId : "";
        this.kind = kind != null ? kind : "";
        this.declarationName = declarationName != null ? declarationName : "";
        this.canonicalId = "callable-v1:" + this.moduleId + ":" + this.kind + ":" + this.declarationName;
    }

    public static CallableId parse(String raw) {
        if (raw == null || !raw.startsWith("callable-v1:")) {
            return null;
        }
        CallableId result = new CallableId(raw);
        if (result.moduleId.isEmpty() || result.kind.isEmpty() || result.declarationName.isEmpty()) {
            return null;
        }
        return result;
    }

    private void parseId() {
        if (!canonicalId.startsWith("callable-v1:")) return;
        String rest = canonicalId.substring(12);
        int colon2 = rest.lastIndexOf(':');
        if (colon2 <= 0) return;
        int colon1 = rest.lastIndexOf(':', colon2 - 1);
        if (colon1 <= 0) return;
        moduleId = rest.substring(0, colon1);
        kind = rest.substring(colon1 + 1, colon2);
        declarationName = rest.substring(colon2 + 1);
    }

    public boolean isEmpty() {
        return canonicalId.isEmpty();
    }

    public String str() {
        return canonicalId;
    }

    public String getModuleId() {
        return moduleId;
    }

    public String getKind() {
        return kind;
    }

    public String getDeclarationName() {
        return declarationName;
    }

    @Override
    public boolean equals(Object o) {
        if (this == o) return true;
        if (!(o instanceof CallableId that)) return false;
        return Objects.equals(canonicalId, that.canonicalId);
    }

    @Override
    public int hashCode() {
        return Objects.hash(canonicalId);
    }

    @Override
    public int compareTo(CallableId o) {
        return this.canonicalId.compareTo(o.canonicalId);
    }

    @Override
    public String toString() {
        return canonicalId;
    }
}
