package org.mosslang.compiler.abi;

import java.util.Objects;

public class MossiRecord {
    public final String id;
    public final String payload;

    public MossiRecord(String id, String payload) {
        this.id = id != null ? id : "";
        this.payload = payload != null ? payload : "";
    }

    @Override
    public boolean equals(Object o) {
        if (this == o) return true;
        if (o == null || getClass() != o.getClass()) return false;
        MossiRecord that = (MossiRecord) o;
        return Objects.equals(id, that.id) && Objects.equals(payload, that.payload);
    }

    @Override
    public int hashCode() {
        return Objects.hash(id, payload);
    }

    @Override
    public String toString() {
        return "MossiRecord{" + id + ", payloadLen=" + payload.length() + "}";
    }
}
