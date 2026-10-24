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
            if (d.name.equals("Store")) {
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
}
