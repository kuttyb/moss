package org.mosslang.compiler.types;

public interface TypeSubstitution {
    TypeRef lookup(int varId);
    boolean contains(int varId);
    void bind(int varId, TypeRef type);
}
