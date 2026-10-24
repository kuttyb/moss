package org.mosslang.compiler.abi;

public record OccurrenceRecord(int ordinal, OccurrenceRole role, int symbolId, int typeId) {
    public long pack() {
        return ((long) ordinal << 48) |
               ((long) (role.getCode() & 0xFF) << 40) |
               ((long) (symbolId & 0xFFFFF) << 20) |
               ((long) (typeId & 0xFFFFF));
    }

    public static OccurrenceRecord unpack(long packed) {
        int ord = (int) (packed >>> 48);
        int roleCode = (int) ((packed >>> 40) & 0xFF);
        int sym = (int) ((packed >>> 20) & 0xFFFFF);
        int typ = (int) (packed & 0xFFFFF);
        return new OccurrenceRecord(ord, OccurrenceRole.fromCode(roleCode), sym, typ);
    }
}
