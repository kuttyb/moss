package org.mosslang.compiler.abi;

import java.util.zip.CRC32;

public class CheckedBodyCodec {
    public static final int MAGIC = 0x4D4F5353; // 'MOSS'
    public static final int ABI_VERSION = 10;

    public static byte[] encode(OccurrenceLedger ledger, byte[] bodyBytes) {
        BytecodeBuffer buf = new BytecodeBuffer();
        buf.writeInt(MAGIC);
        buf.writeInt(ABI_VERSION);
        buf.writeInt(ledger.size());
        
        for (int i = 0; i < ledger.getPackedOccurrences().size(); i++) {
            buf.writeLong(ledger.getPackedOccurrences().get(i));
        }
        
        buf.writeInt(bodyBytes.length);
        buf.writeBytes(bodyBytes);
        
        long crc = buf.computeCrc32();
        buf.writeLong(crc);
        return buf.toByteArray();
    }

    public static boolean validate(byte[] encoded) {
        if (encoded.length < 24) return false;
        int magic = ((encoded[0] & 0xFF) << 24) | ((encoded[1] & 0xFF) << 16) | ((encoded[2] & 0xFF) << 8) | (encoded[3] & 0xFF);
        if (magic != MAGIC) return false;
        int version = ((encoded[4] & 0xFF) << 24) | ((encoded[5] & 0xFF) << 16) | ((encoded[6] & 0xFF) << 8) | (encoded[7] & 0xFF);
        if (version != ABI_VERSION) return false;

        CRC32 crc = new CRC32();
        crc.update(encoded, 0, encoded.length - 8);
        long computed = crc.getValue();

        long stored = 0;
        for (int i = 0; i < 8; i++) {
            stored = (stored << 8) | (encoded[encoded.length - 8 + i] & 0xFFL);
        }
        return computed == stored;
    }
}
