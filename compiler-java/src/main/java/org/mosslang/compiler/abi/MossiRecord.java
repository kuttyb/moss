package org.mosslang.compiler.abi;

import java.util.Objects;
import java.nio.charset.StandardCharsets;
import java.util.Arrays;
import java.nio.charset.CodingErrorAction;
import java.nio.charset.CharacterCodingException;

public class MossiRecord {
    public final String id;
    public final String payload;
    private final byte[] payloadBytes;

    public MossiRecord(String id, String payload) {
        this.id = id != null ? id : "";
        this.payload = payload != null ? payload : "";
        this.payloadBytes = this.payload.getBytes(StandardCharsets.UTF_8);
    }

    private MossiRecord(String id, byte[] payload) {
        this.id = id != null ? id : "";
        this.payloadBytes = payload != null ? payload.clone() : new byte[0];
        String text;
        try {
            text = StandardCharsets.UTF_8.newDecoder()
                    .onMalformedInput(CodingErrorAction.REPORT)
                    .onUnmappableCharacter(CodingErrorAction.REPORT)
                    .decode(java.nio.ByteBuffer.wrap(this.payloadBytes)).toString();
        } catch (CharacterCodingException ex) {
            text = new String(this.payloadBytes, StandardCharsets.ISO_8859_1);
        }
        this.payload = text;
    }

    public static MossiRecord fromBytes(String id, byte[] payload) {
        return new MossiRecord(id, payload);
    }

    public byte[] payloadBytes() {
        return payloadBytes.clone();
    }

    @Override
    public boolean equals(Object o) {
        if (this == o) return true;
        if (o == null || getClass() != o.getClass()) return false;
        MossiRecord that = (MossiRecord) o;
        return Objects.equals(id, that.id) && Arrays.equals(payloadBytes, that.payloadBytes);
    }

    @Override
    public int hashCode() {
        return 31 * Objects.hash(id) + Arrays.hashCode(payloadBytes);
    }

    @Override
    public String toString() {
        return "MossiRecord{" + id + ", payloadLen=" + payloadBytes.length + "}";
    }
}
