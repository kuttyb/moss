package org.mosslang.compiler.abi;

import org.eclipse.collections.api.list.primitive.MutableLongList;
import org.eclipse.collections.impl.list.mutable.primitive.LongArrayList;
import org.eclipse.collections.api.map.primitive.MutableIntObjectMap;
import org.eclipse.collections.impl.map.mutable.primitive.IntObjectHashMap;

public class OccurrenceLedger {
    public static final int ABI_COMPATIBILITY_EPOCH = 100;
    private final MutableLongList packedOccurrences = new LongArrayList();
    private final MutableIntObjectMap<OccurrenceRecord> records = new IntObjectHashMap<>();
    private int nextOrdinal = 1;

    public int record(OccurrenceRole role, int symbolId, int typeId) {
        int ord = nextOrdinal++;
        OccurrenceRecord rec = new OccurrenceRecord(ord, role, symbolId, typeId);
        packedOccurrences.add(rec.pack());
        records.put(ord, rec);
        return ord;
    }

    public OccurrenceRecord getByOrdinal(int ordinal) {
        return records.get(ordinal);
    }

    public int size() {
        return packedOccurrences.size();
    }

    public MutableLongList getPackedOccurrences() {
        return packedOccurrences;
    }

    public long computePrimitiveChecksum() {
        long c = 0;
        for (int i = 0; i < packedOccurrences.size(); i++) {
            c = (c * 31) + packedOccurrences.get(i);
        }
        return c;
    }
}
