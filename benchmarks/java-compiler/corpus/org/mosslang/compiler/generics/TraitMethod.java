package org.mosslang.compiler.generics;

import org.mosslang.compiler.types.TypeRef;
import org.eclipse.collections.api.list.MutableList;

public record TraitMethod(String name, MutableList<TypeRef> paramTypes, TypeRef returnType) {
}
