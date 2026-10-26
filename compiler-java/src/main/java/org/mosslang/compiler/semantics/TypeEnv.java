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
    private final Set<String> boundedStrings = new HashSet<>();
    private final Map<String, java.math.BigInteger> finiteIntegerConstants = new HashMap<>();

    public TypeEnv() {}

    public TypeEnv(TypeEnv other) {
        if (other != null) {
            this.bindings.putAll(other.bindings);
            this.boundedFileIoRequests.addAll(other.boundedFileIoRequests);
            this.boundedStrings.addAll(other.boundedStrings);
            this.finiteIntegerConstants.putAll(other.finiteIntegerConstants);
        }
    }

    public TypeRef get(String key) {
        return bindings.get(key);
    }

    public void put(String key, TypeRef type) {
        bindings.put(key, type);
        boundedFileIoRequests.remove(key);
        boundedStrings.remove(key);
        finiteIntegerConstants.remove(key);
    }

    public void put(String key, String typeString) {
        if (typeString == null || typeString.isEmpty()) {
            bindings.put(key, null);
        } else {
            TypeRef parsed = TypeParser.parseCanonical(typeString);
            bindings.put(key, parsed != null ? parsed : TypeRef.primitive(typeString));
        }
        boundedFileIoRequests.remove(key);
        boundedStrings.remove(key);
        finiteIntegerConstants.remove(key);
    }

    public boolean containsKey(String key) {
        return bindings.containsKey(key);
    }

    public void remove(String key) {
        bindings.remove(key);
        boundedFileIoRequests.remove(key);
        boundedStrings.remove(key);
        finiteIntegerConstants.remove(key);
    }

    public void markBoundedFileIoRequests(String key) { boundedFileIoRequests.add(key); }
    public boolean isBoundedFileIoRequests(String key) { return boundedFileIoRequests.contains(key); }
    public Set<String> boundedFileIoRequestNames() { return Set.copyOf(boundedFileIoRequests); }
    public void invalidateBoundedFileIoRequestsForName(String key) { boundedFileIoRequests.remove(key); }
    public void invalidateBoundedFileIoRequests() { boundedFileIoRequests.clear(); }
    public void putFiniteIntegerConstant(String key, java.math.BigInteger value) {
        if (value != null) finiteIntegerConstants.put(key, value);
        else finiteIntegerConstants.remove(key);
    }
    public java.math.BigInteger finiteIntegerConstant(String key) {
        return finiteIntegerConstants.get(key);
    }
    public void markBoundedString(String key) { boundedStrings.add(key); }
    public boolean isBoundedString(String key) { return boundedStrings.contains(key); }

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
        boundedFileIoRequests.clear();
        boundedStrings.clear();
        finiteIntegerConstants.clear();
    }
}
