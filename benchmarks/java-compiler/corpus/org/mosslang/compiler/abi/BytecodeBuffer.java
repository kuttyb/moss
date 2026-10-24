package org.mosslang.compiler.abi;

import java.util.Arrays;
import java.util.zip.CRC32;

public class BytecodeBuffer {
    private byte[] buffer;
    private int position = 0;

    public BytecodeBuffer() {
        this(256);
    }

    public BytecodeBuffer(int initialCapacity) {
        this.buffer = new byte[initialCapacity];
    }

    private void ensureCapacity(int needed) {
        if (position + needed > buffer.length) {
            int newCap = Math.max(buffer.length * 2, position + needed);
            buffer = Arrays.copyOf(buffer, newCap);
        }
    }

    public void writeByte(int b) {
        ensureCapacity(1);
        buffer[position++] = (byte) b;
    }

    public void writeInt(int val) {
        ensureCapacity(4);
        buffer[position++] = (byte) (val >>> 24);
        buffer[position++] = (byte) (val >>> 16);
        buffer[position++] = (byte) (val >>> 8);
        buffer[position++] = (byte) val;
    }

    public void writeLong(long val) {
        ensureCapacity(8);
        for (int i = 56; i >= 0; i -= 8) {
            buffer[position++] = (byte) (val >>> i);
        }
    }

    public void writeBytes(byte[] src) {
        ensureCapacity(src.length);
        System.arraycopy(src, 0, buffer, position, src.length);
        position += src.length;
    }

    public byte[] toByteArray() {
        return Arrays.copyOf(buffer, position);
    }

    public long computeCrc32() {
        CRC32 crc = new CRC32();
        crc.update(buffer, 0, position);
        return crc.getValue();
    }

    public int position() { return position; }
}
