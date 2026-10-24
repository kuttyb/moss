package org.mosslang.compiler.ast;

import org.eclipse.collections.api.list.MutableList;
import org.eclipse.collections.impl.list.mutable.FastList;

public class Program {
    public MutableList<Line> sourceLines = new FastList<>();
    public String moduleName = "";
    public String mainModule = "";
    public boolean explicitModule = false;
    public MutableList<ModuleImport> imports = new FastList<>();
    public MutableList<Function> functions = new FastList<>();
    public MutableList<Trait> traits = new FastList<>();
    public MutableList<ObjectType> objects = new FastList<>();
    public MutableList<EnumType> enums = new FastList<>();
    public MutableList<Domain> domains = new FastList<>();
    public MutableList<TestDecl> tests = new FastList<>();
    public MutableList<BenchDecl> benchmarks = new FastList<>();
    public MainProc main = null;
    public java.util.Set<String> importedGenericFunctions = new java.util.HashSet<>();
    public java.util.Map<String, java.util.Map<String, org.mosslang.compiler.types.TypeRef>> localBindingTypes = new java.util.HashMap<>();
    public java.util.Map<String, java.util.Map<String, Integer>> localBindingLines = new java.util.HashMap<>();
    public org.mosslang.compiler.types.TypeContext typeContext = null;

    public Program() {}

    public void assertDeclarationTypeConsistency() {
        for (Function f : functions) {
            f.assertConsistent();
        }
        for (ObjectType o : objects) {
            for (Method m : o.methods) {
                m.assertConsistent();
            }
        }
        for (Trait t : traits) {
            for (TraitMethod m : t.methods) {
                m.assertConsistent();
            }
        }
        for (Domain d : domains) {
            for (Handler h : d.handlers) {
                h.assertConsistent();
            }
        }
    }

    @Override
    public String toString() {
        return "Program{module='" + moduleName + "', functions=" + functions.size() +
                ", domains=" + domains.size() + ", objects=" + objects.size() +
                ", enums=" + enums.size() + ", traits=" + traits.size() + "}";
    }
}
