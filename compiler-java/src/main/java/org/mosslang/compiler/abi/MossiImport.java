package org.mosslang.compiler.abi;

import java.util.Objects;

public class MossiImport {
    public final String moduleId;
    public final String binaryHash;
    public final String semanticHash;

    public MossiImport(String moduleId, String binaryHash, String semanticHash) {
        this.moduleId = moduleId != null ? moduleId : "";
        this.binaryHash = binaryHash != null ? binaryHash : "";
        this.semanticHash = semanticHash != null ? semanticHash : "";
    }

    @Override
    public boolean equals(Object o) {
        if (this == o) return true;
        if (o == null || getClass() != o.getClass()) return false;
        MossiImport that = (MossiImport) o;
        return Objects.equals(moduleId, that.moduleId) &&
               Objects.equals(binaryHash, that.binaryHash) &&
               Objects.equals(semanticHash, that.semanticHash);
    }

    @Override
    public int hashCode() {
        return Objects.hash(moduleId, binaryHash, semanticHash);
    }

    @Override
    public String toString() {
        return moduleId + " " + binaryHash + " " + semanticHash;
    }
}
