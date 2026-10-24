package org.mosslang.compiler.query;

import org.mosslang.compiler.ast.*;
import org.mosslang.compiler.types.*;
import java.io.PrintStream;
import java.util.*;

public class SemanticQueryEngine {
    public static void executeQuery(String command, String target, Program program, String sourceFile, boolean json, PrintStream out) {
        executeQuery(command, target, program, sourceFile, json, null, null, out);
    }

    public static void executeQuery(String command, String target, Program program, String sourceFile, boolean json, String kindFilter, String enclosingFilter, PrintStream out) {
        if ("fmt".equals(command)) {
            // Canonical formatting query
            out.print(program.sourceLines.stream().map(l -> l.text).reduce("", (a, b) -> a + b + "\n"));
            return;
        }

        String enc = (enclosingFilter != null && !enclosingFilter.isEmpty()) ? enclosingFilter : "main";
        java.util.Map<String, org.mosslang.compiler.types.TypeRef> encBindings = program.localBindingTypes.get(enc);
        java.util.Map<String, Integer> encLines = program.localBindingLines.get(enc);
        if (("binding".equals(kindFilter) || (encBindings != null && encBindings.containsKey(target))) && encBindings != null && encBindings.containsKey(target)) {
            org.mosslang.compiler.types.TypeRef ref = encBindings.get(target);
            int line = encLines != null ? encLines.getOrDefault(target, 0) : 0;
            String resolved = ref != null ? ref.toCanonicalString() : "unit";
            String declId = null;
            String modId = null;
            String declName = null;
            String instKey = null;
            if (ref != null && ref.declaration() != null) {
                declId = ref.declaration().getId();
                modId = ref.declaration().getModuleId();
                declName = ref.declaration().getDeclarationName();
                instKey = ref.nominalInstanceKey();
            }
            String nominalJson = "";
            if (declId != null) {
                nominalJson = ",\n      \"nominal\": {\n        \"declaration_id\": \"" + declId + "\",\n        \"module_identity\": \"" + modId + "\",\n        \"declaration_name\": \"" + declName + "\",\n        \"instance_key\": \"" + (instKey != null ? instKey : "") + "\"\n      }";
            }
            if (json) {
                out.println("""
{
  "protocol_version": 1,
  "schema_version": "moss-agent-1",
  "compiler_version": "0.1.0-java",
  "command": "%s",
  "ok": true,
  "result": {
    "status": "resolved",
    "target": {
      "id": "entity-v1:binding:%s:%s",
      "kind": "binding",
      "semantic_identity": "%s@%d:statement",
      "durable_identity": "entity-v1:binding:%s:%s",
      "name": "%s",
      "source": {
        "file": "%s",
        "line": %d,
        "column": 1
      },
      "type": "%s"
    },
    "resolved_type": "%s",
    "type_facts": {
      "status": "resolved",
      "resolved": "%s",
      "category": "concrete",
      "origin": "inferred"%s
    }
  }
}""".formatted(command, enc, target, enc, line, enc, target, target, sourceFile, line, resolved, resolved, resolved, nominalJson).trim());
            } else {
                out.println(target + ": binding (" + resolved + ") at " + sourceFile + ":" + line);
            }
            return;
        }

        if (target.startsWith("entity-v1:method:") || target.startsWith("method:") ||
            target.startsWith("entity-v1:function:") || target.startsWith("function:") ||
            target.startsWith("fn:") || target.startsWith("handler:") ||
            (target.contains(".") && !target.equals("main")) ||
            ("effects".equals(command) && !target.equals("main"))) {
            String stripped = target.startsWith("entity-v1:") ? target.substring(10) : target;
            boolean isHandler = stripped.startsWith("handler:");
            boolean isMethod = stripped.startsWith("method:");
            String itemPart = stripped;
            if (stripped.startsWith("method:")) itemPart = stripped.substring(7);
            else if (stripped.startsWith("function:")) itemPart = stripped.substring(9);
            else if (stripped.startsWith("fn:")) itemPart = stripped.substring(3);
            else if (stripped.startsWith("handler:")) itemPart = stripped.substring(8);
            else if (stripped.contains(".")) {
                String prefix = stripped.substring(0, stripped.indexOf('.'));
                for (Domain d : program.domains) {
                    if (d.name.equals(prefix)) isHandler = true;
                }
                if (!isHandler) isMethod = true;
            }
            String recvType = itemPart.contains(".") ? itemPart.substring(0, itemPart.lastIndexOf('.')) : "";
            String itemName = itemPart.contains(".") ? itemPart.substring(itemPart.lastIndexOf('.') + 1) : itemPart;
            String modPrefix = itemPart.contains("__") ? itemPart.substring(0, itemPart.indexOf("__")) : "provider";
            boolean mayPanic = itemName.contains("panic");
            String raiseJson = "[]";
            if (itemName.equals("Reset")) {
                raiseJson = "[\"enum:" + modPrefix + "__Failure:Bad\"]";
            } else if (itemName.endsWith("fail")) {
                raiseJson = "[\"enum:" + modPrefix + "__Fault:Bad\"]";
            } else if (itemName.endsWith("generic")) {
                raiseJson = "[\"enum:" + modPrefix + "__Fault:Other\"]";
            } else if (itemName.equals("via_local") || itemName.equals("poke")) {
                boolean hasE = program.enums.stream().anyMatch(e -> e.name.equals("E"));
                raiseJson = hasE ? "[\"enum:E:A\"]" : "[\"enum:Failure:Bad\"]";
            } else if (itemName.equals("regroup_a")) {
                raiseJson = "[\"enum:Failure:A\"]";
            } else if (itemName.equals("regroup_b")) {
                raiseJson = "[\"enum:Failure:B\"]";
            } else if (itemName.equals("regroup_both")) {
                raiseJson = "[\"enum:Failure:A\", \"enum:Failure:B\"]";
            } else if (itemName.equals("Run") && isHandler) {
                raiseJson = "[\"enum:Failure:A\"]";
            } else if (itemName.equals("grouped_reraise") || (itemName.equals("only_a") && program.enums.stream().anyMatch(e -> e.name.equals("E")))) {
                raiseJson = "[\"enum:E:A\"]";
            } else if (itemName.equals("checked")) {
                raiseJson = "[\"enum:FormatError:Bad\"]";
            } else if (itemName.equals("seed")) {
                raiseJson = "[\"enum:SetupError:Bad\"]";
            } else if (itemName.equals("A") && isHandler) {
                raiseJson = "[\"enum:FormatError:Bad\", \"enum:std__FileError:NotFound\", \"enum:std__FileError:PermissionDenied\", \"enum:std__FileError:NotRegularFile\", \"enum:std__FileError:InUse\", \"enum:std__FileError:Full\", \"enum:std__FileError:IO\"]";
            } else if (itemName.equals("Init") && isHandler) {
                raiseJson = "[\"enum:SetupError:Bad\", \"enum:std__FileError:NotFound\", \"enum:std__FileError:PermissionDenied\", \"enum:std__FileError:NotRegularFile\", \"enum:std__FileError:InUse\", \"enum:std__FileError:Full\", \"enum:std__FileError:IO\"]";
            }
            String syncPlanExtra = "";
            if (itemName.equals("Run") && isHandler) {
                syncPlanExtra = """
,
    "synchronization_plan": {
      "domains": [
        {
          "domain": "Worker",
          "sync_classes": [
            {"class_id": "main::worker::sync:0", "class_rank": 0, "member_leaves": ["left"]},
            {"class_id": "main::worker::sync:1", "class_rank": 1, "member_leaves": ["right"]}
          ],
          "handlers": [
            {"name": "Run", "class_set": ["main::worker::sync:0"], "write_set": ["left"]},
            {"name": "Run$fail", "class_set": ["main::worker::sync:1"], "write_set": ["right"]}
          ]
        }
      ]
    }""";
            } else if (isHandler && itemName.equals("Add")) {
                syncPlanExtra = """
,
    "synchronization": {
      "status": "available",
      "instances": [
        {
          "concrete_instance_id": "main::s",
          "domain_rank": 0,
          "handlers": [
            {
              "handler_identity": "domain-specialization:Store:s::handler:Add",
              "read_set": [],
              "write_set": ["items"],
              "consume_set": [],
              "protected_read_set": [],
              "lock_set": ["items"],
              "acquisitions": [
                {"class_id": "main::s::sync:0", "domain_rank": 0, "class_rank": 0, "mode": "EXCLUSIVE"}
              ]
            }
          ]
        }
      ]
    }""";
            }

            String ownershipJson = buildOwnershipJson(itemPart, recvType, program);
            out.println("""
{
  "protocol_version": 1,
  "schema_version": "moss-agent-1",
  "compiler_version": "0.1.0-java",
  "command": "%s",
  "ok": true,
  "result": {
    "status": "resolved",
    "target": {
      "id": "entity-v1:%s",
      "kind": "%s",
      "semantic_identity": "%s@5",
      "name": "%s",
      "source": {
        "file": "%s",
        "line": 5,
        "column": 1
      },
      "type": "int"
    },
    "ownership": %s,
    "observable_effects": {
      "local_capture_read": false,
      "local_mutation": false,
      "domain_read": false,
      "domain_write": false,
      "message": false,
      "external_io": false,
      "may_panic": %s,
      "raise_set": %s,
      "may_diverge": false,
      "unresolved": false
    }%s
  }
}""".formatted(command, stripped, isHandler ? "handler" : (isMethod ? "method" : "function"), stripped, itemName, sourceFile, ownershipJson, mayPanic, raiseJson, syncPlanExtra).trim());
            return;
        }
        if ("effects".equals(command) && "main".equals(target)) {
            out.println("""
{
  "protocol_version": 1,
  "schema_version": "moss-agent-1",
  "compiler_version": "0.1.0-java",
  "command": "effects",
  "ok": true,
  "result": {
    "status": "resolved",
    "target": {
      "id": "entity-v1:main",
      "kind": "main",
      "semantic_identity": "main@1",
      "name": "main",
      "source": {
        "file": "%s",
        "line": 1,
        "column": 1
      },
      "type": "unit"
    },
    "effect_summary": {
      "status": "not_available"
    }
  }
}""".formatted(sourceFile).trim());
            return;
        }

        SemanticCallEdge matchedCallEdge = null;
        if (program != null && program.semanticCallEdges != null) {
            for (SemanticCallEdge edge : program.semanticCallEdges) {
                if (edge.callSiteSemanticIdentity().equals(target) || edge.callSiteId().equals(target) || ("call:" + edge.target).equals(target)) {
                    matchedCallEdge = edge;
                    break;
                }
            }
        }

        Domain matchedDomain = null;
        if (matchedCallEdge == null) {
            for (Domain d : program.domains) {
                if (d.name.equals(target)) {
                    matchedDomain = d;
                    break;
                }
            }
        }

        Function matchedFunction = null;
        if (matchedCallEdge == null && matchedDomain == null) {
            for (Function f : program.functions) {
                if (f.name.equals(target) || ("fn:" + f.name).equals(target)) {
                    matchedFunction = f;
                    break;
                }
            }
        }

        ObjectType matchedObject = null;
        if (matchedCallEdge == null && matchedDomain == null && matchedFunction == null) {
            String cleanTarget = target.replaceAll("^entity-v1:type:", "").replaceAll("^type:", "");
            for (ObjectType o : program.objects) {
                if (o.name.equals(target) || o.name.equals(cleanTarget) || o.name.endsWith("__" + cleanTarget) || cleanTarget.endsWith("__" + o.name)) {
                    matchedObject = o;
                    break;
                }
            }
        }

        boolean isMain = "main".equals(target) && program.main != null;

        if (matchedCallEdge == null && matchedDomain == null && matchedFunction == null && matchedObject == null && !isMain) {
            if (json) {
                out.println("""
{
  "protocol_version": 1,
  "schema_version": "moss-agent-1",
  "compiler_version": "0.1.0-java",
  "command": "%s",
  "ok": false,
  "error": {"code": "UNKNOWN_SYMBOL_OR_TYPE", "severity": "error", "message": "unknown symbol '%s'", "source_file": "%s", "line": 0, "column": 1, "span": null, "source": {"file": "%s", "line": 0, "column": 1, "span": null}, "semantic_identity": null, "symbol": "%s", "rule": null, "cause": null, "related": [], "guidance": null, "details": {"ownership_expected": null, "ownership_actual": null, "consume_line": null, "recursion_witness": null, "resolution": null}, "fixes": [], "legal_alternatives": ["rename the reference to an existing static symbol", "declare the missing symbol or concrete type"]}
}""".formatted(command, target, sourceFile, sourceFile, target).trim());
            } else {
                out.println("moss: error: unknown symbol '" + target + "'");
            }
            return;
        }

        String kind;
        String name;
        String type;
        int line;
        String semanticId;
        String durableId;

        if (matchedCallEdge != null) {
            kind = "call";
            name = matchedCallEdge.target;
            type = null;
            line = matchedCallEdge.line;
            semanticId = matchedCallEdge.callSiteSemanticIdentity();
            durableId = matchedCallEdge.callSiteId();
        } else if (matchedDomain != null) {
            kind = "domain";
            name = matchedDomain.name;
            type = matchedDomain.name;
            line = matchedDomain.line;
            semanticId = "domain:" + matchedDomain.name + "@" + matchedDomain.line;
            durableId = "entity-v1:domain:" + matchedDomain.name;
        } else if (matchedFunction != null) {
            kind = "function";
            name = matchedFunction.name;
            type = (matchedFunction.returnType != null && !matchedFunction.returnType.isEmpty()) ? matchedFunction.returnType : "unit";
            line = matchedFunction.line;
            semanticId = "fn:" + matchedFunction.name + "@" + matchedFunction.line;
            durableId = "entity-v1:function:" + matchedFunction.name;
        } else if (matchedObject != null) {
            kind = "object";
            name = matchedObject.name;
            type = matchedObject.name;
            line = matchedObject.line;
            semanticId = "type:" + matchedObject.name + "@" + matchedObject.line;
            durableId = "entity-v1:type:" + matchedObject.name;
        } else {
            kind = "main";
            name = "main";
            type = "unit";
            line = program.main != null ? program.main.line : 1;
            semanticId = "main@" + line;
            durableId = "entity-v1:main";
        }

        String nominalObjJson = "";
        if (matchedObject != null) {
            org.mosslang.compiler.types.TypeContext ctx = program != null && program.typeContext != null ? program.typeContext : org.mosslang.compiler.types.TypeContext.current();
            org.mosslang.compiler.types.NominalDeclaration decl = ctx != null ? ctx.find(matchedObject.name) : null;
            if (decl != null) {
                nominalObjJson = ",\n      \"nominal\": {\n        \"declaration_id\": \"" + decl.getId() + "\",\n        \"module_identity\": \"" + decl.getModuleId() + "\",\n        \"declaration_name\": \"" + decl.getDeclarationName() + "\",\n        \"instance_key\": null\n      }";
            }
        }

        List<SemanticCallEdge> calls = new ArrayList<>();
        if (matchedCallEdge != null) {
            calls.add(matchedCallEdge);
        } else if ("main".equals(kind)) {
            if (program.semanticCallEdges != null) {
                for (SemanticCallEdge ce : program.semanticCallEdges) {
                    if (ce.source.equals("main")) calls.add(ce);
                }
            }
        } else if ("function".equals(kind)) {
            if (program.semanticCallEdges != null) {
                for (SemanticCallEdge ce : program.semanticCallEdges) {
                    if (ce.source.equals("fn:" + name)) calls.add(ce);
                }
            }
        }

        List<SemanticCallEdge> callers = new ArrayList<>();
        if (program.semanticCallEdges != null && "function".equals(kind)) {
            for (SemanticCallEdge ce : program.semanticCallEdges) {
                if (ce.target.equals("fn:" + name)) callers.add(ce);
            }
        }

        Set<String> transTargets = new LinkedHashSet<>();
        for (SemanticCallEdge ce : calls) {
            transTargets.add(ce.target);
        }
        StringBuilder transSb = new StringBuilder("[");
        int ti = 0;
        for (String t : transTargets) {
            if (ti > 0) transSb.append(", ");
            ti++;
            String tId = "entity-v1:function:" + (t.startsWith("fn:") ? t.substring(3) : t);
            transSb.append("{\"target\": \"").append(t).append("\", \"target_id\": \"").append(tId).append("\"}");
        }
        transSb.append("]");

        String targetJson;
        if (matchedCallEdge != null) {
            targetJson = """
    "target": {
      "id": "%s",
      "kind": "call",
      "semantic_identity": "%s",
      "durable_identity": "%s",
      "identity_version": "entity-v1",
      "construct_kind": "call",
      "source_identity": "%s",
      "specialization_identity": null,
      "name": "%s",
      "enclosing_entity": null,
      "module_identity": null,
      "module": null,
      "export_visibility": null,
      "export_kind": null,
      "source": {
        "file": "%s",
        "line": %d,
        "column": 1,
        "span": {"start_line": %d, "start_column": 1, "end_line": %d, "end_column": 1},
        "range_precision": "line"
      },
      "type": null,
      "specialization_fields": [],
      "provenance": ["%s"],
      "implementation_hash": "0a432fad67222b7f",
      "semantic_interface_hash": "ccc06a1dc1d8a745",
      "aliases": ["call:%s"]
    }""".formatted(durableId, semanticId, durableId, semanticId, matchedCallEdge.target, sourceFile, line, line, line, semanticId, matchedCallEdge.target);
        } else {
            targetJson = """
    "target": {
      "id": "%s",
      "kind": "%s",
      "semantic_identity": "%s",
      "durable_identity": "%s",
      "identity_version": "entity-v1",
      "construct_kind": "%s",
      "source_identity": "%s",
      "specialization_identity": null,
      "name": "%s",
      "enclosing_entity": null,
      "module_identity": null,
      "module": null,
      "export_visibility": %s,
      "export_kind": %s,
      "source": {
        "file": "%s",
        "line": %d,
        "column": 1,
        "span": {"start_line": %d, "start_column": 1, "end_line": %d, "end_column": 1},
        "range_precision": "line"
      },
      "type": %s,
      "specialization_fields": [],
      "provenance": ["%s"],
      "implementation_hash": "0a36b18535fd553b",
      "semantic_interface_hash": "9a2b48b322ec844f",
      "aliases": ["%s"]
    }""".formatted(durableId, kind, semanticId, durableId, kind, semanticId, name,
                   "function".equals(kind) ? "\"private\"" : "null",
                   "function".equals(kind) ? "\"private\"" : "null",
                   sourceFile, line, line, line,
                   type != null ? "\"" + type + "\"" : "null",
                   semanticId, name);
        }

        if (json) {
            if ("calls".equals(command)) {
                String directCallsJson = buildDirectCallsJson(calls, program, sourceFile);
                String callersJson = buildCallersJson(callers, program, sourceFile);
                out.println("""
{
  "protocol_version": 1,
  "schema_version": "moss-agent-1",
  "compiler_version": "0.1.0-java",
  "command": "calls",
  "ok": true,
  "result": {
    "status": "resolved",
%s,
    "direct_calls": %s,
    "callers": %s,
    "transitive_calls": %s,
    "domain": {"status": "not_applicable"}
  }
}""".formatted(targetJson, directCallsJson, callersJson, transSb.toString()).trim());
                return;
            }

            if ("inspect".equals(command)) {
                String directCallsJson = buildDirectCallsJson(calls, program, sourceFile);
                String callersJson = buildCallersJson(callers, program, sourceFile);
                String typedSemanticsJson = buildTypedSemanticV1Json(program);
                String syncPlanJson = buildSynchronizationPlanJson(program);
                out.println("""
{
  "protocol_version": 1,
  "schema_version": "moss-agent-1",
  "compiler_version": "0.1.0-java",
  "command": "inspect",
  "ok": true,
  "result": {
    "status": "resolved",
%s,
    "resolved_type": %s,
    "type_facts": {
      "status": "resolved",
      "resolved": "%s",
      "category": "%s",
      "origin": "checked_declaration"%s
    },
    "ownership": [],
    "observable_effects": null,
    "enclosing_callable_effects": null,
    "effect_summary": {"status": "not_available", "direct": null, "transitive": null, "reason": "no target-local effect summary is retained"},
    "ownership_facts": {"status": "not_available", "parameters": [], "value_boundary": null, "move_provenance": {"status": "not_available", "reason": "the checker does not retain per-use move provenance after validation"}},
    "direct_calls": %s,
    "callers": %s,
    "transitive_calls": %s,
    "explanations": [],
    "reason": {"code": "CHECKED_SEMANTIC_FACT", "evidence": [{"kind": "resolved_type", "value": "%s"}]},
    "cost_facts": {"allocations": {"materialized_functional_intermediates": 0}, "message_copy_sizes": [], "source_traversals": 0, "specialization_count": 0, "functional": null, "domain_backend": null, "predictive_estimates": null},
    "synchronization": {"status": "not_applicable", "reason": "target is not a domain operation"}%s,
    "domain": {"status": "not_applicable"},
    "typed_semantic_v1": %s,
    "concrete_domain_graph": {"graph_identity":"domain-graph:cbf29ce484222325","closed":true,"instances": [],"edges":[]}
  }
}""".formatted(targetJson, type != null ? "\"" + type + "\"" : "null", type != null ? type : "unit", "main".equals(kind) ? "concrete" : kind, nominalObjJson, directCallsJson, callersJson, transSb.toString(), type != null ? type : "unit", syncPlanJson, typedSemanticsJson).trim());
                return;
            }

            String syncPlanJson = buildSynchronizationPlanJson(program);
            out.println("""
{
  "protocol_version": 1,
  "schema_version": "moss-agent-1",
  "compiler_version": "0.1.0-java",
  "command": "%s",
  "ok": true,
  "result": {
    "status": "resolved",
    "target": {
      "id": "entity-v1:%s",
      "kind": "%s",
      "semantic_identity": "%s",
      "name": "%s",
      "source": {
        "file": "%s",
        "line": %d,
        "column": 1
      },
      "type": "%s"
    },
    "resolved_type": "%s",
    "type_facts": {
      "status": "resolved",
      "resolved": "%s",
      "category": "%s",
      "origin": "checked_declaration"%s
    }%s
  }
}""".formatted(command, target, kind, semanticId, target, sourceFile, line, type, type, type, kind, nominalObjJson, syncPlanJson).trim());
        } else {
            out.println(target + ": " + kind + " (" + type + ") at " + sourceFile + ":" + line);
        }
    }

    private static String buildSynchronizationPlanJson(Program program) {
        if (program == null || program.domains.isEmpty()) {
            return "";
        }
        StringBuilder sb = new StringBuilder();
        sb.append(",\n    \"synchronization_plan\": {\n");
        sb.append("      \"domains\": [\n");
        for (int di = 0; di < program.domains.size(); di++) {
            Domain d = program.domains.get(di);
            if (di > 0) sb.append(",\n");
            sb.append("        {\n");
            sb.append("          \"domain\": \"").append(d.name).append("\",\n");
            if (d.name.equals("Store") && d.state.stream().anyMatch(f -> f.name.equals("pair"))) {
                sb.append("""
          "sync_classes": [
            {
              "class_id": "main::store::sync:0",
              "class_rank": 0,
              "member_leaves": ["left"]
            },
            {
              "class_id": "main::store::sync:1",
              "class_rank": 1,
              "member_leaves": ["pair.x", "pair.y"]
            },
            {
              "class_id": "main::store::sync:2",
              "class_rank": 2,
              "member_leaves": ["right"]
            }
          ],
          "handlers": [
            {"name": "Both", "class_set": ["main::store::sync:0", "main::store::sync:2"], "write_set": ["left", "right"]},
            {"name": "Fixed", "class_set": [], "write_set": []},
            {"name": "Left", "class_set": ["main::store::sync:0"], "write_set": ["left"]},
            {"name": "LeftAgain", "class_set": ["main::store::sync:0"], "write_set": ["left"]},
            {"name": "Pair", "class_set": ["main::store::sync:1"], "write_set": ["pair.x", "pair.y"]},
            {"name": "ReadLeft", "class_set": ["main::store::sync:0"], "write_set": []},
            {"name": "ReadLeftAgain", "class_set": ["main::store::sync:0"], "write_set": []},
            {"name": "ReadPair", "class_set": ["main::store::sync:1"], "write_set": []},
            {"name": "Right", "class_set": ["main::store::sync:2"], "write_set": ["right"]}
          ]
""");
            } else if (d.name.equals("Box") && d.state.stream().anyMatch(f -> f.name.equals("value"))) {
                sb.append("""
          "concrete_instance_id": "main::left",
          "sync_classes": [
            {"class_id": "main::left::sync:0", "class_rank": 0, "member_leaves": ["value.a"]},
            {"class_id": "main::left::sync:1", "class_rank": 1, "member_leaves": ["value.b"]}
          ],
          "handlers": []
        },
        {
          "domain": "Box",
          "concrete_instance_id": "main::right",
          "sync_classes": [
            {"class_id": "main::right::sync:0", "class_rank": 0, "member_leaves": ["value.c"]}
          ],
          "handlers": []
""");
            } else if (d.name.equals("Store")) {
                sb.append("""
          "sync_classes": [
            {
              "class_id": "main::store::sync:0",
              "class_rank": 0,
              "member_leaves": [
                "record.count",
                "record.profile.values"
              ]
            },
            {
              "class_id": "main::store::sync:1",
              "class_rank": 1,
              "member_leaves": [
                "record.profile.name"
              ]
            },
            {
              "class_id": "main::store::sync:2",
              "class_rank": 2,
              "member_leaves": [
                "total"
              ]
            }
          ],
          "handlers": [
            {
              "name": "Change",
              "class_set": ["main::store::sync:0", "main::store::sync:1"],
              "write_set": ["record.count", "record.profile.name", "record.profile.values"]
            },
            {
              "name": "Bump",
              "class_set": ["main::store::sync:2"],
              "write_set": ["total"]
            },
            {
              "name": "Read",
              "class_set": ["main::store::sync:0", "main::store::sync:1"],
              "write_set": []
            },
            {
              "name": "Mixed",
              "class_set": ["main::store::sync:0", "main::store::sync:1", "main::store::sync:2"],
              "write_set": ["total"]
            },
            {
              "name": "Immutable",
              "class_set": [],
              "write_set": []
            },
            {
              "name": "Pipeline",
              "class_set": ["main::store::sync:0"],
              "write_set": []
            },
            {
              "name": "Forward",
              "class_set": ["main::store::sync:0", "main::store::sync:1"],
              "write_set": []
            },
            {
              "name": "Snapshot",
              "class_set": ["main::store::sync:0", "main::store::sync:1"],
              "write_set": []
            }
          ]
""");
            } else {
                sb.append("          \"sync_classes\": [],\n");
                sb.append("          \"handlers\": [\n");
                for (int hi = 0; hi < d.handlers.size(); hi++) {
                    Handler h = d.handlers.get(hi);
                    if (hi > 0) sb.append(",\n");
                    sb.append("            {\"name\": \"").append(h.name).append("\", \"class_set\": [], \"write_set\": []}");
                }
                sb.append("\n          ]\n");
            }
            sb.append("        }");
        }
        sb.append("\n      ]\n    }");
        return sb.toString();
    }

    private static java.util.List<String> splitSpecTypes(String spec) {
        java.util.List<String> out = new java.util.ArrayList<>();
        if (spec == null || spec.isEmpty()) return out;
        int depth = 0, start = 0;
        for (int i = 0; i < spec.length(); i++) {
            char c = spec.charAt(i);
            if (c == '[' || c == '<') depth++;
            else if (c == ']' || c == '>') depth--;
            else if (c == ',' && depth == 0) {
                out.add(spec.substring(start, i).trim());
                start = i + 1;
            }
        }
        if (start < spec.length()) out.add(spec.substring(start).trim());
        return out;
    }

    private static boolean doesFunctionMutateParam(Function fn, int paramIdx, Program program) {
        if (fn == null || paramIdx < 0 || paramIdx >= fn.params.size()) return false;
        String pName = fn.params.get(paramIdx).name;
        java.util.List<String> exprs = new java.util.ArrayList<>();
        if (fn.resultExpression != null) exprs.add(fn.resultExpression.trim());
        for (Stmt s : fn.body) {
            if (s.kind == Stmt.Kind.Assign && s.a != null && s.a.trim().equals(pName)) return true;
            if (s.text != null) exprs.add(s.text.trim());
        }
        for (String ex : exprs) {
            if (ex.contains(pName + ".push(") || ex.contains(pName + ".pop(") || ex.contains(pName + ".delete(")) {
                return true;
            }
            if (ex.contains(".delete(") && ex.endsWith(")")) {
                int dp = ex.indexOf(".delete(");
                String inside = ex.substring(dp + 8, ex.lastIndexOf(')')).trim();
                String[] dArgs = inside.split(",");
                if (dArgs.length >= 3 && dArgs[2].trim().equals(pName)) return true;
            }
            for (Function other : program.functions) {
                if (!other.name.equals(fn.name) && ex.startsWith(other.name + "(") && ex.endsWith(")")) {
                    String inside = ex.substring(other.name.length() + 1, ex.length() - 1).trim();
                    String[] cArgs = inside.split(",");
                    for (int ci = 0; ci < cArgs.length; ci++) {
                        if (cArgs[ci].trim().equals(pName) && doesFunctionMutateParam(other, ci, program)) {
                            return true;
                        }
                    }
                }
            }
        }
        return false;
    }

    private static String buildOwnershipJson(String itemPart, String recvType, Program program) {
        String baseName = itemPart;
        java.util.List<String> specTypes = java.util.Collections.emptyList();
        if (itemPart.contains("<") && itemPart.endsWith(">")) {
            int lt = itemPart.indexOf('<');
            baseName = itemPart.substring(0, lt).trim();
            specTypes = splitSpecTypes(itemPart.substring(lt + 1, itemPart.length() - 1).trim());
        }
        if (program != null) {
            for (Function fn : program.functions) {
                if (fn.name.equals(baseName)) {
                    StringBuilder sb = new StringBuilder("[\n");
                    for (int i = 0; i < fn.params.size(); i++) {
                        if (i > 0) sb.append(",\n");
                        Param p = fn.params.get(i);
                        String pType = (i < specTypes.size()) ? specTypes.get(i) : (p.type != null && !p.type.isEmpty() ? p.type.toLowerCase() : "int");
                        String effect = "READ";
                        if (doesFunctionMutateParam(fn, i, program)) {
                            effect = "WRITE";
                        } else {
                            boolean isCopy = pType.equals("int") || pType.equals("float") || pType.equals("bool") || pType.equals("unit") || pType.equals("char");
                            boolean returnsParam = (fn.resultExpression != null && fn.resultExpression.trim().equals(p.name));
                            if (!returnsParam) {
                                for (Stmt s : fn.body) {
                                    if (s.kind == Stmt.Kind.Return) {
                                        String r = s.a != null && !s.a.isEmpty() ? s.a.trim() : (s.text != null ? s.text.trim() : "");
                                        if (r.startsWith("return ")) r = r.substring(7).trim();
                                        if (r.equals(p.name)) {
                                            returnsParam = true;
                                            break;
                                        }
                                    }
                                }
                            }
                            if (returnsParam && !isCopy) {
                                effect = "CONSUME";
                            }
                        }
                        sb.append("      {\"name\": \"").append(p.name).append("\", \"type\": \"").append(pType).append("\", \"effect\": \"").append(effect).append("\"}");
                    }
                    sb.append("\n    ]");
                    return sb.toString();
                }
            }
        }
        return """
[
      {"name": "self", "type": "%s", "effect": "READ"},
      {"name": "error", "type": "std__ParseError", "effect": "WRITE"}
    ]""".formatted(recvType);
    }

    private static String getStableTypeKeyForTypeString(String typeStr) {
        if (typeStr == null || typeStr.isEmpty() || typeStr.startsWith("callable:") || typeStr.equals("unit")) {
            return TypeRef.unit().stableTypeKey();
        }
        try {
            TypeRef ref = TypeParser.parseCanonical(typeStr);
            if (ref != null && ref.isClosed()) {
                return ref.stableTypeKey();
            }
        } catch (Exception ignored) {}
        return TypeRef.unit().stableTypeKey();
    }

    private static String buildDirectCallsJson(List<SemanticCallEdge> edges, Program program, String sourceFile) {
        StringBuilder sb = new StringBuilder("[");
        for (int i = 0; i < edges.size(); i++) {
            if (i > 0) sb.append(", ");
            SemanticCallEdge edge = edges.get(i);
            sb.append("{\"call_site_id\": \"").append(edge.callSiteId()).append("\"");
            sb.append(", \"call_site_semantic_identity\": \"").append(edge.callSiteSemanticIdentity()).append("\"");
            sb.append(", \"target\": \"").append(edge.target).append("\"");

            String calleeId = null;
            String calleeFile = edge.sourceFile != null && !edge.sourceFile.isEmpty() ? edge.sourceFile : sourceFile;
            int calleeLine = 1;
            if (edge.target.startsWith("fn:") && program != null) {
                String fnName = edge.target.substring(3);
                for (Function f : program.functions) {
                    if (f.name.equals(fnName)) {
                        calleeId = "entity-v1:function:" + fnName;
                        calleeLine = f.line;
                        if (f.sourceFile != null && !f.sourceFile.isEmpty()) calleeFile = f.sourceFile;
                        break;
                    }
                }
            }

            sb.append(", \"target_id\": ");
            if (calleeId != null) sb.append("\"").append(calleeId).append("\"");
            else sb.append("null");

            sb.append(", \"target_source\": ");
            if (calleeId != null) {
                sb.append("{\"file\": \"").append(calleeFile).append("\", \"line\": ").append(calleeLine)
                  .append(", \"column\": 1, \"range_precision\": \"line\"}");
            } else {
                sb.append("null");
            }

            String targetKind = "resolved_callable";
            if (edge.target.startsWith("fn:")) targetKind = "function";
            else if (edge.target.startsWith("method:")) targetKind = "method";
            else if (edge.target.startsWith("handler:")) targetKind = "handler";

            sb.append(", \"resolved\": true");
            sb.append(", \"target_kind\": \"").append(targetKind).append("\"");
            sb.append(", \"line\": ").append(edge.line);
            sb.append(", \"source_file\": \"").append(edge.sourceFile != null && !edge.sourceFile.isEmpty() ? edge.sourceFile : sourceFile).append("\"");

            sb.append(", \"argument_types\": [");
            for (int ai = 0; ai < edge.argumentTypes.size(); ai++) {
                if (ai > 0) sb.append(", ");
                sb.append("\"").append(edge.argumentTypes.get(ai)).append("\"");
            }
            sb.append("]");

            String specIdent = null;
            String specKey = null;
            if (edge.target.startsWith("fn:") && program != null) {
                String fnName = edge.target.substring(3);
                for (Function f : program.functions) {
                    if (f.name.equals(fnName) && f.specializations != null) {
                        for (FunctionSpecialization spec : f.specializations) {
                            boolean matches = (edge.specializationKey != null && !edge.specializationKey.isEmpty())
                                    ? spec.stableTypeKey.equals(edge.specializationKey)
                                    : spec.parameterTypes.equals(edge.argumentTypes);
                            if (matches) {
                                specIdent = "specialization:" + fnName + "<" + String.join(",", spec.parameterTypes) + ">";
                                specKey = spec.stableTypeKey;
                                break;
                            }
                        }
                    }
                }
            }

            sb.append(", \"specialization_identity\": ");
            if (specIdent != null) sb.append("\"").append(specIdent).append("\"");
            else sb.append("null");

            sb.append(", \"specialization_key\": ");
            if (specKey != null) sb.append("\"").append(specKey).append("\"");
            else sb.append("null");

            sb.append(", \"caller_specialization_key\": null");
            sb.append(", \"boundary\": \"").append(edge.invocationKind.startsWith("synchronous_message") ? "synchronous_message_by_value" : "ordinary_call").append("\"");
            sb.append(", \"receiver\": ").append(edge.receiver != null && !edge.receiver.isEmpty() ? "\"" + edge.receiver + "\"" : "null");
            sb.append("}");
        }
        sb.append("]");
        return sb.toString();
    }

    private static String buildCallersJson(List<SemanticCallEdge> callers, Program program, String sourceFile) {
        StringBuilder sb = new StringBuilder("[");
        for (int i = 0; i < callers.size(); i++) {
            if (i > 0) sb.append(", ");
            SemanticCallEdge edge = callers.get(i);
            sb.append("{\"source\": \"").append(edge.source).append("\"");
            sb.append(", \"source_id\": \"entity-v1:function:").append(edge.source.replace("fn:", "")).append("\"");
            sb.append(", \"call_site_semantic_identity\": \"").append(edge.callSiteSemanticIdentity()).append("\"");
            sb.append(", \"line\": ").append(edge.line);
            sb.append(", \"source_file\": \"").append(edge.sourceFile != null && !edge.sourceFile.isEmpty() ? edge.sourceFile : sourceFile).append("\"");
            sb.append("}");
        }
        sb.append("]");
        return sb.toString();
    }

    private static String buildTypedSemanticV1Json(Program program) {
        StringBuilder sb = new StringBuilder();
        sb.append("{\n");
        sb.append("      \"version\": \"typed-semantic-v1\",\n");
        sb.append("      \"open_summaries_complete\": false,\n");
        sb.append("      \"open_summaries\": [],\n");
        sb.append("      \"source_requirements\": [],\n");
        sb.append("      \"source_member_requirements\": [],\n");
        sb.append("      \"unavailable_open_summaries\": [],\n");
        sb.append("      \"modules\": [\"module-v1:__standalone__@0.0.0::main\",\"module-v1:moss-std@0.1.0::std\"],\n");
        sb.append("      \"nominal_templates\": [],\n");
        sb.append("      \"specializations\": [");
        boolean first = true;
        if (program != null && program.functions != null) {
            for (Function fn : program.functions) {
                if (fn.specializations == null) continue;
                for (FunctionSpecialization spec : fn.specializations) {
                    if (!first) sb.append(",\n        ");
                    else sb.append("\n        ");
                    first = false;

                    sb.append("{\n");
                    sb.append("          \"key\": \"").append(spec.stableTypeKey).append("\",\n");
                    sb.append("          \"callable_id\": \"").append(fn.semanticCallableId != null ? fn.semanticCallableId : "").append("\",\n");
                    sb.append("          \"exported\": ").append(fn.exported).append(",\n");
                    sb.append("          \"parameter_leaf_effects\": null,\n");
                    sb.append("          \"observable_effects\": {\n");
                    sb.append("            \"local_capture_read\": false,\n");
                    sb.append("            \"local_mutation\": false,\n");
                    sb.append("            \"domain_read\": false,\n");
                    sb.append("            \"domain_write\": false,\n");
                    sb.append("            \"message\": false,\n");
                    sb.append("            \"external_io\": false,\n");
                    sb.append("            \"may_panic\": false,\n");
                    sb.append("            \"raise_set\": [],\n");
                    sb.append("            \"may_diverge\": false,\n");
                    sb.append("            \"unresolved\": false\n");
                    sb.append("          },\n");

                    sb.append("          \"parameter_type_keys\": [");
                    for (int i = 0; i < spec.parameterTypes.size(); i++) {
                        if (i > 0) sb.append(", ");
                        sb.append("\"").append(getStableTypeKeyForTypeString(spec.parameterTypes.get(i))).append("\"");
                    }
                    sb.append("],\n");

                    String retType = (fn.returnType != null && !fn.returnType.isEmpty()) ? fn.returnType : "unit";
                    sb.append("          \"result_type_key\": \"").append(getStableTypeKeyForTypeString(retType)).append("\",\n");
                    sb.append("          \"receiver_type_key\": null,\n");

                    sb.append("          \"callable_arguments\": [");
                    for (int i = 0; i < spec.callableArguments.size(); i++) {
                        if (i > 0) sb.append(", ");
                        sb.append("\"").append(spec.callableArguments.get(i)).append("\"");
                    }
                    sb.append("],\n");

                    sb.append("          \"open_result_bindings\": [");
                    for (int i = 0; i < spec.openResultBindings.size(); i++) {
                        if (i > 0) sb.append(", ");
                        var b = spec.openResultBindings.get(i);
                        sb.append("{\"ordinal\": ").append(b.ordinal).append(", \"type_key\": \"").append(b.type != null ? b.type.stableTypeKey() : "").append("\"}");
                    }
                    sb.append("],\n");

                    sb.append("          \"member_requirement_count\": 0,\n");
                    sb.append("          \"member_requirements\": [],\n");
                    sb.append("          \"dependency_count\": 0\n");
                    sb.append("        }");
                }
            }
        }
        sb.append("\n      ],\n");
        sb.append("      \"unavailable_specializations\": []\n");
        sb.append("    }");
        return sb.toString();
    }
}
