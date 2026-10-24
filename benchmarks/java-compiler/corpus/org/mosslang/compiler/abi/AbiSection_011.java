package org.mosslang.compiler.abi;

import java.util.zip.CRC32;
import org.eclipse.collections.api.list.primitive.MutableByteList;
import org.eclipse.collections.impl.list.mutable.primitive.ByteArrayList;

public class AbiSection_011 {
    private final int sectionId = 11;
    private final MutableByteList payload = new ByteArrayList();

    public void appendData(byte[] data) {
        for (byte b : data) payload.add(b);
    }

    public long calculateChecksum() {
        CRC32 crc = new CRC32();
        crc.update(payload.toArray());
        return crc.getValue();
    }

    public int getSectionId() { return sectionId; }
    public int size() { return payload.size(); }
}
