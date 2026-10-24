package org.mosslang.compiler.abi;

import java.util.ArrayList;
import java.util.List;
import java.util.Objects;

public class MossiInterface {
    public String moduleId = "";
    public String compiler = "moss-java-0.1.0";
    public String backendFingerprint = "fingerprint-v10";
    public String backendRustc = "rustc-1.80.0";

    public List<MossiImport> imports = new ArrayList<>();
    public List<MossiRecord> binaryContract = new ArrayList<>();
    public List<MossiRecord> semanticContract = new ArrayList<>();
    public List<MossiRecord> privateSemanticDependencies = new ArrayList<>();
    public List<MossiRecord> provenance = new ArrayList<>();

    public String binaryHash = "";
    public String semanticHash = "";
    public String interfaceHash = "";
    public String digest = "";

    public MossiInterface() {}

    public MossiInterface(String moduleId) {
        this.moduleId = moduleId != null ? moduleId : "";
    }

    public void addImport(String moduleId, String binaryHash, String semanticHash) {
        this.imports.add(new MossiImport(moduleId, binaryHash, semanticHash));
    }

    public void addFunction(String name, String signature, String checkedBodyHash) {
        String id = "fn:" + name + "@1";
        String payload = "export fn " + name + " " + signature + "\n" +
                         (checkedBodyHash != null && !checkedBodyHash.isEmpty() ? "  body_hash " + checkedBodyHash + "\n" : "");
        this.semanticContract.add(new MossiRecord(id, payload));
    }

    public void addDomain(String name, List<String> routes) {
        String id = "domain:" + name;
        String routeList = routes != null ? String.join(", ", routes) : "";
        String payload = "export domain " + name + "\n  domainroutes(" + routeList + ")\n";
        this.semanticContract.add(new MossiRecord(id, payload));
    }

    public void addCheckedBody(String callableId, List<CheckedStmt> statements) {
        String bodyHash = CheckedBodyCodec.computeCheckedBodyHashV1(statements);
        String irBytes = CheckedBodyCodec.encodeCheckedBodyV1(statements);
        String id = "checked-body-v1:" + callableId;
        String payload = "checked-body-v1\n" +
                         "callable_id " + callableId + "\n" +
                         "semantic_hash " + bodyHash + "\n" +
                         "checked_ir\n" + irBytes;
        this.semanticContract.add(new MossiRecord(id, payload));
    }

    @Override
    public boolean equals(Object o) {
        if (this == o) return true;
        if (o == null || getClass() != o.getClass()) return false;
        MossiInterface that = (MossiInterface) o;
        return Objects.equals(moduleId, that.moduleId) &&
               Objects.equals(compiler, that.compiler) &&
               Objects.equals(backendFingerprint, that.backendFingerprint) &&
               Objects.equals(backendRustc, that.backendRustc) &&
               Objects.equals(imports, that.imports) &&
               Objects.equals(binaryContract, that.binaryContract) &&
               Objects.equals(semanticContract, that.semanticContract) &&
               Objects.equals(privateSemanticDependencies, that.privateSemanticDependencies) &&
               Objects.equals(provenance, that.provenance);
    }

    @Override
    public int hashCode() {
        return Objects.hash(moduleId, compiler, backendFingerprint, backendRustc, imports,
                            binaryContract, semanticContract, privateSemanticDependencies, provenance);
    }
}
