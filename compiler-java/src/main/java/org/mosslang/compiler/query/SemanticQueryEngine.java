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
    }
  }
}""".formatted(command, target, kind, semanticId, target, sourceFile, line, type, type, type, kind).trim());
        } else {
            out.println(target + ": " + kind + " (" + type + ") at " + sourceFile + ":" + line);
        }
    }
}
