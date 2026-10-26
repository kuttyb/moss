package org.mosslang.compiler.semantics;

import org.mosslang.compiler.types.TypeParser;
import org.mosslang.compiler.types.TypeRef;

import java.util.Collections;
import java.util.HashMap;
import java.util.Map;
import java.util.HashSet;
import java.util.Set;

public class TypeEnv {
    private final Map<String, TypeRef> bindings = new HashMap<>();
    private final Set<String> boundedFileIoRequests = new HashSet<>();

    public TypeEnv() {}

    public TypeEnv(TypeEnv other) {
        if (other != null) {
            this.bindings.putAll(other.bindings);
            this.boundedFileIoRequests.addAll(other.boundedFileIoRequests);
        }
    }

    public TypeRef get(String key) {
        return bindings.get(key);
    }

    public void put(String key, TypeRef type) {
        bindings.put(key, type);
        boundedFileIoRequests.remove(key);
    }

    public void put(String key, String typeString) {
        if (typeString == null || typeString.isEmpty()) {
            bindings.put(key, null);
        } else {
            TypeRef parsed = TypeParser.parseCanonical(typeString);
            bindings.put(key, parsed != null ? parsed : TypeRef.primitive(typeString));
        }
        boundedFileIoRequests.remove(key);
    }

    public boolean containsKey(String key) {
        return bindings.containsKey(key);
    }

    public void remove(String key) {
        bindings.remove(key);
        boundedFileIoRequests.remove(key);
    }

    public void markBoundedFileIoRequests(String key) { boundedFileIoRequests.add(key); }
    public boolean isBoundedFileIoRequests(String key) { return boundedFileIoRequests.contains(key); }
    public Set<String> boundedFileIoRequestNames() { return Set.copyOf(boundedFileIoRequests); }
    public void invalidateBoundedFileIoRequestsForName(String key) { boundedFileIoRequests.remove(key); }
    public void invalidateBoundedFileIoRequests() { boundedFileIoRequests.clear(); }

    public Map<String, TypeRef> getBindings() {
        return Collections.unmodifiableMap(bindings);
    }

    public TypeEnv copy() {
        return new TypeEnv(this);
    }

    public int size() {
        return bindings.size();
    }

    public boolean isEmpty() {
        return bindings.isEmpty();
    }

    public void clear() {
        bindings.clear();
    }
}
