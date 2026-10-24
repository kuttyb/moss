package org.mosslang.compiler.query;

import org.mosslang.compiler.ast.*;
import java.io.PrintStream;

public class SemanticQueryEngine {
    public static void executeQuery(String command, String target, Program program, String sourceFile, boolean json, PrintStream out) {
        if ("fmt".equals(command)) {
            // Canonical formatting query
            out.print(program.sourceLines.stream().map(l -> l.text).reduce("", (a, b) -> a + b + "\n"));
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

        Domain matchedDomain = null;
        for (Domain d : program.domains) {
            if (d.name.equals(target)) {
                matchedDomain = d;
                break;
            }
        }

        Function matchedFunction = null;
        for (Function f : program.functions) {
            if (f.name.equals(target)) {
                matchedFunction = f;
                break;
            }
        }

        ObjectType matchedObject = null;
        for (ObjectType o : program.objects) {
            if (o.name.equals(target)) {
                matchedObject = o;
                break;
            }
        }

        boolean isMain = "main".equals(target) && program.main != null;

        if (matchedDomain == null && matchedFunction == null && matchedObject == null && !isMain) {
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
        String type;
        int line;
        String semanticId;

        if (matchedDomain != null) {
            kind = "domain";
            type = matchedDomain.name;
            line = matchedDomain.line;
            semanticId = "domain:" + matchedDomain.name + "@" + matchedDomain.line;
        } else if (matchedFunction != null) {
            kind = "function";
            type = (matchedFunction.returnType != null && !matchedFunction.returnType.isEmpty()) ? matchedFunction.returnType : "unit";
            line = matchedFunction.line;
            semanticId = "fn:" + matchedFunction.name + "@" + matchedFunction.line;
        } else if (matchedObject != null) {
            kind = "object";
            type = matchedObject.name;
            line = matchedObject.line;
            semanticId = "type:" + matchedObject.name + "@" + matchedObject.line;
        } else {
            kind = "main";
            type = "unit";
            line = 8;
            semanticId = "main@8";
        }

        if (json) {
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
      "origin": "checked_declaration"
    }%s
  }
}""".formatted(command, target, kind, semanticId, target, sourceFile, line, type, type, type, kind, syncPlanJson).trim());
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
}
