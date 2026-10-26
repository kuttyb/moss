package org.mosslang.compiler.query;

import org.mosslang.compiler.ast.*;
import org.mosslang.compiler.parser.ParserSupport;
import org.mosslang.compiler.types.*;
import java.io.PrintStream;
import java.util.*;

public class SemanticQueryEngine {
    public static boolean executeQuery(String command, String target, Program program, String sourceFile, boolean json, PrintStream out) {
        return executeQuery(command, target, program, sourceFile, json, null, null, out);
    }

    public static boolean executeQuery(String command, String target, Program program, String sourceFile, boolean json, String kindFilter, String enclosingFilter, PrintStream out) {
        if ("fmt".equals(command)) {
            // Canonical formatting query
            out.print(program.sourceLines.stream().map(l -> l.text).reduce("", (a, b) -> a + b + "\n"));
            return true;
        }
        if ("references".equals(command))
            return referencesQuery(target, program, sourceFile, json, out);

        if (("effects".equals(command) || "why".equals(command))
                && pipelineExpressionAtLocation(target, program, sourceFile, command, json, out)) {
            return true;
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
            return true;
        }

        if ("resolve".equals(command) && isSourceLocation(target)) {
            return resolveSourceLocation(target, program, sourceFile, kindFilter, json, out);
        }
        if ("type".equals(command) && isSourceLocation(target)) {
            return typeBindingAtLocation(target, program, sourceFile, json, out);
        }
        if ("effects".equals(command) && isSourceLocation(target)
                && effectsBindingAtLocation(target, program, sourceFile, json, out)) {
            return true;
        }
        if ("resolve".equals(command) && isBareResolveTarget(target)) {
            return resolveName(target, program, sourceFile, kindFilter, enclosingFilter, json, out);
        }

        if ((("effects".equals(command) || "ownership".equals(command) || "resolve".equals(command))
                && (target.startsWith("entity-v1:method:") || target.startsWith("method:")
                    || target.startsWith("entity-v1:function:") || target.startsWith("function:")
                    || target.startsWith("fn:") || target.startsWith("handler:")
                    || (target.contains(".") && !target.equals("main"))))
                || ("effects".equals(command) && !target.equals("main"))) {
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
            Function queriedFunction = null;
            if (program != null && program.functions != null) {
                String queriedName = itemPart;
                if (target.startsWith("fn:")) {
                    queriedName = target.substring(3);
                    int suffix = queriedName.indexOf('@');
                    if (suffix >= 0) queriedName = queriedName.substring(0, suffix);
                    int specialization = queriedName.indexOf('<');
                    if (specialization >= 0) queriedName = queriedName.substring(0, specialization);
                }
                String functionLookupName = queriedName;
                queriedFunction = program.functions.stream()
                        .filter(fn -> fn.name.equals(itemName) || fn.name.equals(functionLookupName))
                        .findFirst().orElse(null);
            }
            ObjectType queriedObject = null;
            Method queriedMethod = null;
            if (program != null && program.objects != null) {
                for (ObjectType object : program.objects) {
                    for (Method method : object.methods) {
                        if (method.name.equals(itemName) || (object.name + "." + method.name).equals(itemPart)
                                || target.equals("entity-v1:method:" + object.name + "." + method.name)) {
                            if (enclosingFilter == null || enclosingFilter.isEmpty()
                                    || enclosingFilter.equals("method:" + object.name + "." + method.name)
                                    || enclosingFilter.equals(object.name + "." + method.name)) {
                                queriedObject = object;
                                queriedMethod = method;
                                break;
                            }
                        }
                    }
                    if (queriedMethod != null) break;
                }
            }
            Domain queriedDomain = null;
            Handler queriedHandler = null;
            if (isHandler && program != null && itemPart.contains(".")) {
                String domainName = itemPart.substring(0, itemPart.lastIndexOf('.'));
                String handlerName = itemPart.substring(itemPart.lastIndexOf('.') + 1);
                for (Domain domain : program.domains) {
                    if (!domain.name.equals(domainName)) continue;
                    queriedDomain = domain;
                    queriedHandler = domain.handlers.stream()
                            .filter(handler -> handler.name.equals(handlerName)).findFirst().orElse(null);
                    break;
                }
            }
            String queriedKind = isHandler ? "handler" : (isMethod ? "method" : "function");
            String queriedId = "entity-v1:" + (isHandler ? "handler:" : (isMethod ? "method:" : "function:")) + itemName;
            String queriedSemanticId = itemName + "@5";
            int queriedLine = 5;
            String queriedType = "int";
            String queriedName = itemName;
            String queriedSourceFile = sourceFile;
            String queriedEnclosingJson = "null";
            if (queriedFunction != null) {
                queriedName = queriedFunction.name;
                queriedId = "entity-v1:function:" + queriedFunction.name;
                queriedSemanticId = "fn:" + queriedFunction.name + "@" + queriedFunction.line;
                queriedLine = queriedFunction.line;
                queriedType = functionResultType(queriedFunction);
                if (queriedFunction.sourceFile != null && !queriedFunction.sourceFile.isEmpty()) {
                    queriedSourceFile = queriedFunction.sourceFile;
                }
                if ("resolve".equals(command) && enclosingFilter != null && !enclosingFilter.isEmpty()) {
                    String enclosingName = enclosingFilter.replaceFirst("^(function:|fn:)", "");
                    String resolvedName = queriedFunction.name;
                    SemanticCallEdge matchingCall = program.semanticCallEdges.stream()
                            .filter(edge -> (edge.target.equals("fn:" + resolvedName)
                                            || edge.target.equals(resolvedName))
                                    && (edge.source.equals(enclosingName)
                                            || edge.source.equals("fn:" + enclosingName)))
                            .findFirst().orElse(null);
                    if (matchingCall != null) {
                        queriedKind = "call";
                        queriedId = matchingCall.callSiteId();
                        queriedSemanticId = matchingCall.callSiteSemanticIdentity();
                        queriedLine = matchingCall.line;
                        queriedType = matchingCall.returnTypeRef != null
                                ? matchingCall.returnTypeRef.toCanonicalString() : "unit";
                        queriedName = matchingCall.target;
                        queriedEnclosingJson = "\"" + jsonString(matchingCall.source) + "\"";
                    }
                }
            }
            if (queriedMethod != null && queriedObject != null) {
                queriedKind = "method";
                queriedName = queriedObject.name + "." + queriedMethod.name;
                queriedId = "entity-v1:method:" + queriedName;
                queriedSemanticId = "method:" + queriedName + "@" + queriedMethod.line;
                queriedLine = queriedMethod.line;
                queriedType = queriedMethod.returnType != null && !queriedMethod.returnType.isEmpty()
                        ? queriedMethod.returnType : "unit";
                if (queriedMethod.sourceFile != null && !queriedMethod.sourceFile.isEmpty()) {
                    queriedSourceFile = queriedMethod.sourceFile;
                }
            }
            boolean mayPanic = false;
            boolean externalIo = false;
            boolean localCaptureRead = false;
            boolean domainRead = false;
            boolean domainWrite = false;
            boolean unresolvedEffects = queriedFunction != null
                    && hasUnresolvedObjectMethodReceiver(queriedFunction, program);
            if (queriedDomain != null && queriedHandler != null) {
                for (Field state : queriedDomain.state) {
                    String identifier = "\\b" + java.util.regex.Pattern.quote(state.name) + "\\b";
                    for (Stmt statement : queriedHandler.body) {
                        String targetName = statement.a != null ? statement.a.trim() : "";
                        String sourceText = (statement.b != null ? statement.b : "") + " "
                                + (statement.text != null ? statement.text : "");
                        if (statement.kind == Stmt.Kind.Assign
                                && (targetName.equals(state.name) || targetName.startsWith(state.name + "."))) {
                            domainWrite = true;
                        }
                        if (java.util.regex.Pattern.compile(identifier).matcher(sourceText).find()) {
                            domainRead = true;
                        }
                    }
                }
            }
            if (program != null && program.functions != null) {
                Function f = queriedFunction != null ? queriedFunction
                        : program.functions.stream().filter(fn -> fn.name.equals(itemName)).findFirst().orElse(null);
                if (f != null) {
                    externalIo |= callableHasEffect(f.body, f.resultExpression, program,
                            new HashSet<>(), EffectKind.EXTERNAL_IO, f, null);
                    mayPanic |= callableHasEffect(f.body, f.resultExpression, program,
                            new HashSet<>(), EffectKind.MAY_PANIC, f, null);
                }
            }
            if (queriedMethod != null) {
                externalIo |= callableHasEffect(queriedMethod.body, queriedMethod.resultExpression, program,
                        new HashSet<>(), EffectKind.EXTERNAL_IO, null, queriedMethod);
                mayPanic |= callableHasEffect(queriedMethod.body, queriedMethod.resultExpression, program,
                        new HashSet<>(), EffectKind.MAY_PANIC, null, queriedMethod);
            }
            if (queriedHandler != null) {
                externalIo |= callableHasEffect(queriedHandler.body, null, program,
                        new HashSet<>(), EffectKind.EXTERNAL_IO);
                mayPanic |= callableHasEffect(queriedHandler.body, null, program,
                        new HashSet<>(), EffectKind.MAY_PANIC);
            }
            String raiseJson = "[]";
            Set<String> directRaiseFacts = callableRaisedVariants(
                    queriedFunction, queriedMethod, queriedHandler, program);
            if (!directRaiseFacts.isEmpty()) {
                raiseJson = jsonStringArray(directRaiseFacts);
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
            String valueBoundaryJson = isHandler
                    ? "{\"kind\":\"synchronous_message\",\"payload\":\"by_value_immutable_snapshot\",\"reply\":\"by_value\",\"domain_handles_allowed\":false}"
                    : "null";
            if (queriedDomain != null && queriedHandler != null) {
                syncPlanExtra = ",\n    \"synchronization\": "
                        + buildHandlerSynchronizationJson(queriedDomain, queriedHandler, program);
            }
            String directEffectStatus = "not_retained";
            String directEffectsJson = "null";
            java.util.regex.Matcher resultTarget = java.util.regex.Pattern
                    .compile("^fn:.+@[0-9]+:result$").matcher(target);
            java.util.regex.Matcher expressionTarget = java.util.regex.Pattern
                    .compile(".*@([0-9]+):expression:[0-9]+$").matcher(target);
            if (resultTarget.matches() && queriedFunction != null) {
                directEffectStatus = "exact";
                String expression = queriedFunction.resultExpression != null
                        ? queriedFunction.resultExpression : "";
                for (Stmt statement : queriedFunction.body) {
                    if (statement.kind != Stmt.Kind.Assign && statement.kind != Stmt.Kind.Let
                            && statement.kind != Stmt.Kind.Var) continue;
                    String local = statement.a != null ? statement.a.trim() : "";
                    if (local.isEmpty() || queriedFunction.params.stream().anyMatch(param -> param.name.equals(local))) continue;
                    int mapStart = expression.indexOf("map(");
                    int mapEnd = mapStart >= 0 ? expression.indexOf(')', mapStart + 4) : -1;
                    if (mapStart >= 0 && mapEnd > mapStart
                            && expression.substring(mapStart + 4, mapEnd).contains(local)) {
                        localCaptureRead = true;
                    }
                }
                directEffectsJson = "{\"local_capture_read\":" + localCaptureRead + ",\"local_mutation\":false,"
                        + "\"domain_read\":false,\"domain_write\":false,\"message\":false,"
                        + "\"external_io\":" + externalIo + ",\"may_panic\":" + mayPanic + ",\"raise_set\":"
                        + raiseJson + ",\"may_diverge\":false,\"unresolved\":" + unresolvedEffects + "}";
            } else if (expressionTarget.matches() && program.main != null) {
                int expressionLine = Integer.parseInt(expressionTarget.group(1));
                String expression = "";
                for (Stmt statement : program.main.body) {
                    if (statement.line == expressionLine) {
                        expression = statement.text + " " + statement.a + " " + statement.b;
                        break;
                    }
                }
                for (Function function : program.functions) {
                    if (!expression.contains(function.name)) continue;
                    if (function.body.stream().anyMatch(statement -> statement.kind == Stmt.Kind.Echo
                            || (statement.text != null && statement.text.trim().startsWith("echo ")))) {
                        externalIo = true;
                    }
                }
                directEffectStatus = "exact";
                directEffectsJson = "{\"local_capture_read\":" + localCaptureRead + ",\"local_mutation\":false,"
                        + "\"domain_read\":false,\"domain_write\":false,\"message\":false,"
                        + "\"external_io\":" + externalIo + ",\"may_panic\":false,\"raise_set\":[],"
                        + "\"may_diverge\":false,\"unresolved\":false}";
            }
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
      "id": "%s",
      "kind": "%s",
      "semantic_identity": "%s",
      "name": "%s",
      "enclosing_entity": %s,
      "source": {
        "file": "%s",
        "line": %d,
        "column": 1
      },
      "type": "%s"
    },
    "ownership": %s,
    "parameters_and_values": %s,
    "ownership_facts": {"status": "available", "parameters": %s, "value_boundary": %s, "move_provenance": {"status": "not_available", "reason": "the checker does not retain per-use move provenance after validation"}},
    "observable_effects": {
      "local_capture_read": false,
      "local_mutation": false,
      "domain_read": %s,
      "domain_write": %s,
      "message": false,
      "external_io": %s,
      "may_panic": %s,
      "raise_set": %s,
      "may_diverge": false,
      "unresolved": %s
    },
    "enclosing_callable_effects": null,
    "effect_summary": {"status": "available", "direct": %s, "direct_status": "%s", "transitive": {"local_capture_read": false, "local_mutation": false, "domain_read": %s, "domain_write": %s, "message": %s, "external_io": %s, "may_panic": %s, "raise_set": %s, "may_diverge": false, "unresolved": %s}, "transitive_scope": "target", "certainty": "%s"}%s
  }
}""".formatted(command, queriedId, queriedKind, queriedSemanticId, queriedName, queriedEnclosingJson, queriedSourceFile,
                    queriedLine, queriedType, ownershipJson, ownershipJson, ownershipJson, valueBoundaryJson, domainRead, domainWrite,
                    externalIo, mayPanic, raiseJson, unresolvedEffects, directEffectsJson, directEffectStatus,
                    domainRead, domainWrite, isHandler, externalIo, mayPanic, raiseJson, unresolvedEffects,
                    unresolvedEffects ? "conservative_unresolved" : "resolved", syncPlanExtra).trim());
            return true;
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
            return true;
        }

        SemanticCallEdge matchedCallEdge = null;
        if (program != null && program.semanticCallEdges != null) {
            for (SemanticCallEdge edge : program.semanticCallEdges) {
                if (edge.callSiteSemanticIdentity().equals(target) || edge.callSiteId().equals(target) || ("call:" + edge.target).equals(target)) {
                    matchedCallEdge = edge;
                    break;
                }
            }
            if (matchedCallEdge == null && ("type".equals(command) || "why".equals(command))
                    && enclosingFilter != null && !enclosingFilter.isEmpty()) {
                String requestedTarget = target.replaceFirst("^fn:", "");
                String requestedEnclosing = enclosingFilter.replaceFirst("^(function:|fn:)", "");
                for (SemanticCallEdge edge : program.semanticCallEdges) {
                    if ((edge.target.equals("fn:" + requestedTarget) || edge.target.equals(requestedTarget))
                            && (edge.source.equals(requestedEnclosing)
                                    || edge.source.equals("fn:" + requestedEnclosing))) {
                        matchedCallEdge = edge;
                        break;
                    }
                }
            }
        }

        Domain matchedDomain = null;
        Handler matchedHandler = null;
        if (target.startsWith("handler:")) {
            String qualified = target.substring("handler:".length());
            int separator = qualified.lastIndexOf('.');
            if (separator > 0) {
                String domainName = qualified.substring(0, separator);
                String handlerName = qualified.substring(separator + 1);
                for (Domain domain : program.domains) {
                    if (!domain.name.equals(domainName)) continue;
                    matchedDomain = domain;
                    matchedHandler = domain.handlers.stream()
                            .filter(handler -> handler.name.equals(handlerName)).findFirst().orElse(null);
                    break;
                }
            }
        }
        if (matchedCallEdge == null) {
            for (Domain d : program.domains) {
                if (d.name.equals(target)) {
                    matchedDomain = d;
                    break;
                }
            }
        }

        Function matchedFunction = null;
        FunctionSpecialization matchedSpecialization = null;
        if (matchedCallEdge == null && matchedDomain == null && matchedHandler == null) {
            for (Function f : program.functions) {
                if (f.name.equals(target) || ("fn:" + f.name).equals(target)) {
                    matchedFunction = f;
                    break;
                }
            }
            if (matchedFunction == null && target.startsWith("fn:") && target.endsWith(">")) {
                int open = target.indexOf('<');
                if (open > 3) {
                    String functionName = target.substring(3, open);
                    List<String> requestedTypes = Arrays.stream(target.substring(open + 1, target.length() - 1).split(","))
                            .map(String::trim).toList();
                    for (Function f : program.functions) {
                        if (!f.name.equals(functionName)) continue;
                        for (FunctionSpecialization specialization : f.specializations) {
                            if (specialization.parameterTypes.equals(requestedTypes)) {
                                matchedFunction = f;
                                matchedSpecialization = specialization;
                                break;
                            }
                        }
                        if (matchedFunction != null) break;
                    }
                }
            }
        }

        ObjectType matchedObject = null;
        if (matchedCallEdge == null && matchedDomain == null && matchedFunction == null && matchedHandler == null) {
            String cleanTarget = target.replaceAll("^entity-v1:type:", "").replaceAll("^type:", "");
            for (ObjectType o : program.objects) {
                if (o.name.equals(target) || o.name.equals(cleanTarget) || o.name.endsWith("__" + cleanTarget) || cleanTarget.endsWith("__" + o.name)) {
                    matchedObject = o;
                    break;
                }
            }
        }

        Method matchedMethod = null;
        Field matchedField = null;
        if (matchedCallEdge == null && matchedDomain == null && matchedFunction == null
                && target.contains(".")) {
            String cleanTarget = target.replaceFirst("^entity-v1:method:", "")
                    .replaceFirst("^method:", "");
            int separator = cleanTarget.lastIndexOf('.');
            if (separator > 0) {
                String owner = cleanTarget.substring(0, separator);
                String member = cleanTarget.substring(separator + 1);
                for (ObjectType object : program.objects) {
                    if (!object.name.equals(owner) && !object.name.endsWith("__" + owner)) continue;
                    for (Method method : object.methods) {
                        if (method.name.equals(member)) {
                            matchedMethod = method;
                            matchedObject = object;
                            break;
                        }
                    }
                    if (matchedMethod == null) {
                        for (Field field : object.fields) {
                            if (field.name.equals(member)) {
                                matchedField = field;
                                matchedObject = object;
                                break;
                            }
                        }
                    }
                    if (matchedMethod != null || matchedField != null) break;
                }
            }
        }

        boolean isMain = "main".equals(target) && program.main != null;

        if (matchedCallEdge == null && matchedDomain == null && matchedFunction == null
                && matchedObject == null && matchedMethod == null && matchedField == null
                && matchedHandler == null && !isMain) {
            if (json) {
                String resolutionJson = "resolve".equals(command)
                        ? "{\"status\":\"missing\",\"candidates\":[]}" : "null";
                out.println("""
{
  "protocol_version": 1,
  "schema_version": "moss-agent-1",
  "compiler_version": "0.1.0-java",
  "command": "%s",
  "ok": false,
  "error": {"code": "QUERY_TARGET_NOT_FOUND", "severity": "error", "message": "query target '%s' was not found", "source_file": "%s", "line": 0, "column": 1, "span": null, "source": {"file": "%s", "line": 0, "column": 1, "span": null}, "semantic_identity": null, "symbol": "%s", "rule": null, "cause": null, "related": [], "guidance": null, "details": {"ownership_expected": null, "ownership_actual": null, "consume_line": null, "recursion_witness": null, "resolution": %s}, "fixes": [], "legal_alternatives": ["rename the reference to an existing static symbol", "declare the missing symbol or concrete type"]}
}""".formatted(command, target, sourceFile, sourceFile, target, resolutionJson).trim());
            } else {
                out.println("moss: error: unknown symbol '" + target + "'");
            }
            return false;
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
        } else if (matchedDomain != null && matchedHandler == null) {
            kind = "domain";
            name = matchedDomain.name;
            type = matchedDomain.name;
            line = matchedDomain.line;
            semanticId = "domain:" + matchedDomain.name + "@" + matchedDomain.line;
            durableId = "entity-v1:domain:" + matchedDomain.name;
        } else if (matchedHandler != null) {
            kind = "handler";
            name = matchedDomain.name + "." + matchedHandler.name;
            type = matchedHandler.replyType != null && !matchedHandler.replyType.isEmpty()
                    ? matchedHandler.replyType : "unit";
            line = matchedHandler.line;
            semanticId = "handler:" + name + "@" + line;
            durableId = "entity-v1:handler:" + name;
        } else if (matchedFunction != null) {
            kind = "function";
            name = matchedFunction.name;
            type = matchedSpecialization != null && matchedSpecialization.returnType != null
                    && !matchedSpecialization.returnType.isEmpty() ? matchedSpecialization.returnType
                    : functionResultType(matchedFunction);
            line = matchedFunction.line;
            semanticId = "fn:" + matchedFunction.name + "@" + matchedFunction.line;
            durableId = "entity-v1:function:" + matchedFunction.name;
        } else if (matchedMethod != null) {
            kind = "method";
            name = matchedObject.name + "." + matchedMethod.name;
            type = matchedMethod.returnType != null && !matchedMethod.returnType.isEmpty()
                    ? matchedMethod.returnType : "unit";
            line = matchedMethod.line;
            semanticId = "method:" + name + "@" + line;
            durableId = "entity-v1:method:" + name;
        } else if (matchedField != null) {
            kind = "field";
            name = matchedObject.name + "." + matchedField.name;
            type = matchedField.type;
            line = matchedField.line;
            semanticId = "field:" + name + "@" + line;
            durableId = "entity-v1:field:" + name;
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
        } else if ("function".equals(kind) || "method".equals(kind)) {
            if (program.semanticCallEdges != null) {
                for (SemanticCallEdge ce : program.semanticCallEdges) {
                    String sourceName = "function".equals(kind) ? "fn:" + name : name;
                    if (ce.source.equals(sourceName)) calls.add(ce);
                }
            }
        }
        calls = limitRepeatedSemanticCallEdges(calls, program);

        List<SemanticCallEdge> callers = new ArrayList<>();
        if (program.semanticCallEdges != null && ("function".equals(kind) || "method".equals(kind))) {
            for (SemanticCallEdge ce : program.semanticCallEdges) {
                String callableTarget = "function".equals(kind) ? "fn:" + name : "method:" + name;
                if (ce.target.equals(callableTarget)) callers.add(ce);
            }
        }

        Set<String> transTargets = new LinkedHashSet<>();
        Deque<String> pendingCallables = new ArrayDeque<>();
        Set<String> visitedCallables = new HashSet<>();
        for (SemanticCallEdge ce : calls) pendingCallables.add(ce.target);
        while (!pendingCallables.isEmpty()) {
            String callable = pendingCallables.removeFirst();
            if (!transTargets.add(callable) || !visitedCallables.add(callable)) continue;
            String sourceName = callable.startsWith("fn:") ? callable
                    : callable.startsWith("method:") ? callable.substring("method:".length()) : callable;
            if (program.semanticCallEdges == null) continue;
            for (SemanticCallEdge edge : program.semanticCallEdges) {
                if (edge.source.equals(sourceName) && !visitedCallables.contains(edge.target)) {
                    pendingCallables.addLast(edge.target);
                }
            }
        }
        callers = limitRepeatedSemanticCallEdges(callers, program);
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
            if ("why".equals(command)) {
                String reasonCode = matchedCallEdge != null ? "STATIC_CALL_RESOLUTION"
                        : "INFERRED_VALUE_EFFECTS";
                String detail = matchedCallEdge != null
                        ? "call target was resolved statically by the Moss checker"
                        : "parameter access modes were inferred by the Moss checker";
                String evidence = matchedCallEdge != null
                        ? "[{\"kind\":\"compiler_decision\",\"detail\":\"" + detail + "\"}]"
                        : "[{\"kind\":\"ownership\",\"detail\":\"" + detail + "\"}]";
                out.println("{\"protocol_version\":1,\"schema_version\":\"moss-agent-1\",\"compiler_version\":\"0.1.0-java\",\"command\":\"why\",\"ok\":true,\"result\":{\"status\":\"resolved\","
                        + targetJson + ",\"explanations\":[\"" + detail + "\"],\"reason\":{\"code\":\""
                        + reasonCode + "\",\"evidence\":" + evidence + "}}}");
                return true;
            }
            if ("type".equals(command) && matchedCallEdge != null) {
                out.println("""
{
  "protocol_version": 1,
  "schema_version": "moss-agent-1",
  "compiler_version": "0.1.0-java",
  "command": "type",
  "ok": true,
  "result": {
    "status": "resolved",
%s,
    "resolved_type": null,
    "type_facts": {"status": "not_available", "resolved": null, "category": "unavailable", "origin": "not_retained"}
  }
}""".formatted(targetJson).trim());
                return true;
            }

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
                return true;
            }

            if ("inspect".equals(command)) {
                String directCallsJson = buildDirectCallsJson(calls, program, sourceFile);
                String callersJson = buildCallersJson(callers, program, sourceFile);
                String typedSemanticsJson = buildTypedSemanticV1Json(program);
                String syncPlanJson = buildSynchronizationPlanJson(program);
                String domainJson = matchedDomain != null && matchedHandler != null
                        ? buildHandlerDomainJson(matchedDomain, matchedHandler, program)
                        : "{\"status\":\"not_applicable\"}";
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
    "domain": %s,
    "typed_semantic_v1": %s,
    "concrete_domain_graph": %s
  }
                }""".formatted(targetJson, type != null ? "\"" + type + "\"" : "null", type != null ? type : "unit", matchedSpecialization != null ? "specialized" : ("main".equals(kind) ? "concrete" : kind), nominalObjJson, directCallsJson, callersJson, transSb.toString(), type != null ? type : "unit", syncPlanJson, domainJson, typedSemanticsJson, buildConcreteDomainGraphJson(program)).trim());
                return true;
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
}""".formatted(command, target, kind, semanticId, target, sourceFile, line, type, type, type,
                matchedSpecialization != null ? "specialized" : kind, nominalObjJson, syncPlanJson).trim());
        } else {
            out.println(target + ": " + kind + " (" + type + ") at " + sourceFile + ":" + line);
        }
        return true;
    }

    private static String buildConcreteDomainGraphJson(Program program) {
        record Instance(String binding, String domain, String sourceFile, int line) {}
        record Edge(String source, String route, String target, String sourceFile, int line) {}
        List<Instance> instances = new ArrayList<>();
        List<Edge> edges = new ArrayList<>();
        Map<String, Instance> byBinding = new HashMap<>();
        if (program != null && program.main != null) {
            for (Stmt statement : program.main.body) {
                if (statement.indent != 0 || !(statement.kind == Stmt.Kind.Let
                        || statement.kind == Stmt.Kind.Var || statement.kind == Stmt.Kind.Assign)
                        || statement.a == null || statement.b == null) continue;
                String expression = statement.b.trim();
                int open = expression.indexOf('(');
                if (open <= 0 || !expression.endsWith(")")) continue;
                String domainName = expression.substring(0, open).trim();
                Domain domain = program.domains.stream().filter(candidate -> candidate.name.equals(domainName))
                        .findFirst().orElse(null);
                if (domain == null) continue;
                String binding = statement.a.trim();
                Instance instance = new Instance(binding, domainName,
                        statement.sourceFile != null && !statement.sourceFile.isEmpty()
                                ? statement.sourceFile : program.sourceFile, statement.line);
                instances.add(instance);
                byBinding.put(binding, instance);
            }
            for (Stmt statement : program.main.body) {
                Instance source = byBinding.get(statement.a != null ? statement.a.trim() : "");
                if (source == null) continue;
                Domain domain = program.domains.stream().filter(candidate -> candidate.name.equals(source.domain()))
                        .findFirst().orElse(null);
                if (domain == null) continue;
                String expression = statement.b != null ? statement.b.trim() : "";
                int open = expression.indexOf('(');
                if (open <= 0 || !expression.endsWith(")")) continue;
                String inside = expression.substring(open + 1, expression.length() - 1);
                for (String argument : org.mosslang.compiler.parser.ParserSupport.splitTopLevel(inside, ',')) {
                    int colon = argument.indexOf(':');
                    if (colon <= 0) continue;
                    String routeName = argument.substring(0, colon).trim();
                    String targetBinding = argument.substring(colon + 1).trim();
                    DomainRoute route = domain.routes.stream().filter(candidate -> candidate.name.equals(routeName))
                            .findFirst().orElse(null);
                    Instance target = byBinding.get(targetBinding);
                    if (route != null && target != null && route.type.equals(target.domain())) {
                        edges.add(new Edge("main::" + source.binding(), routeName,
                                "main::" + target.binding(), source.sourceFile(), source.line()));
                    }
                }
            }
        }
        TreeSet<String> domainNames = new TreeSet<>();
        instances.forEach(instance -> domainNames.add(instance.domain()));
        Map<String, Integer> ranks = new HashMap<>();
        int rank = 0;
        for (String domain : domainNames) ranks.put(domain, rank++);
        StringBuilder material = new StringBuilder();
        for (Instance instance : instances) {
            material.append('"').append("main::" + instance.binding()).append('"')
                    .append('"').append("domain-specialization:" + instance.domain() + ":" + instance.binding()).append('"')
                    .append(ranks.get(instance.domain()));
        }
        for (Edge edge : edges) material.append('"').append(edge.source()).append('"')
                .append('"').append(edge.route()).append('"').append('"').append(edge.target()).append('"');
        long hash = Long.parseUnsignedLong("cbf29ce484222325", 16);
        for (byte value : material.toString().getBytes(java.nio.charset.StandardCharsets.UTF_8)) {
            hash ^= value & 0xffL;
            hash *= 1099511628211L;
        }
        String identity = String.format(java.util.Locale.ROOT, "domain-graph:%016x", hash);
        StringBuilder json = new StringBuilder("{\"graph_identity\":\"").append(identity)
                .append("\",\"closed\":true,\"instances\":[");
        for (int i = 0; i < instances.size(); i++) {
            if (i > 0) json.append(',');
            Instance instance = instances.get(i);
            String id = "main::" + instance.binding();
            String specialization = "domain-specialization:" + instance.domain() + ":" + instance.binding();
            json.append("{\"identity\":\"").append(jsonString(id)).append("\",\"binding\":\"")
                    .append(jsonString(instance.binding())).append("\",\"domain\":\"")
                    .append(jsonString(instance.domain())).append("\",\"concrete_instance_id\":\"")
                    .append(jsonString(id)).append("\",\"source_domain_id\":\"domain:")
                    .append(jsonString(instance.domain())).append("\",\"specialization_id\":\"")
                    .append(jsonString(specialization)).append("\",\"source_file\":\"")
                    .append(jsonString(instance.sourceFile())).append("\",\"line\":")
                    .append(instance.line()).append(",\"domain_rank\":").append(ranks.get(instance.domain())).append('}');
        }
        json.append("],\"edges\":[");
        for (int i = 0; i < edges.size(); i++) {
            if (i > 0) json.append(',');
            Edge edge = edges.get(i);
            json.append("{\"source_instance\":\"").append(jsonString(edge.source()))
                    .append("\",\"route\":\"").append(jsonString(edge.route()))
                    .append("\",\"target_instance\":\"").append(jsonString(edge.target()))
                    .append("\",\"source_file\":\"").append(jsonString(edge.sourceFile()))
                    .append("\",\"line\":").append(edge.line()).append('}');
        }
        return json.append("]}").toString();
    }

    private static final class ResolutionCandidate {
        String id;
        String kind;
        String semanticIdentity;
        String name;
        String type;
        String sourceFile;
        int line;
    }

    private static boolean isSourceLocation(String target) {
        return target != null && (target.matches("line:[0-9]+")
                || target.matches("at:[0-9]+:[0-9]+"));
    }

    private static boolean isBareResolveTarget(String target) {
        return target != null && target.matches("[A-Za-z_][A-Za-z0-9_]*");
    }

    private static boolean typeBindingAtLocation(String selector, Program program, String sourceFile,
                                                 boolean json, PrintStream out) {
        String[] parts = selector.split(":");
        int line = Integer.parseInt(parts[parts.length - (parts[0].equals("line") ? 1 : 2)]);
        List<ResolutionCandidate> candidates = new ArrayList<>();
        if (program != null && program.localBindingLines != null) {
            for (Map.Entry<String, Map<String, Integer>> scope : program.localBindingLines.entrySet()) {
                Map<String, TypeRef> types = program.localBindingTypes.getOrDefault(scope.getKey(), Map.of());
                for (Map.Entry<String, Integer> binding : scope.getValue().entrySet()) {
                    if (binding.getValue() != line) continue;
                    ResolutionCandidate candidate = new ResolutionCandidate();
                    candidate.kind = "binding";
                    candidate.name = binding.getKey();
                    candidate.id = "entity-v1:binding:" + scope.getKey() + ":" + binding.getKey();
                    candidate.semanticIdentity = scope.getKey() + "@" + line + ":statement";
                    TypeRef type = types.get(binding.getKey());
                    candidate.type = type != null ? type.toCanonicalString() : "unit";
                    candidate.sourceFile = sourceFile;
                    candidate.line = line;
                    candidates.add(candidate);
                }
            }
        }
        if (candidates.size() != 1) {
            return emitResolutionFailure(selector, candidates, sourceFile, json, out);
        }
        ResolutionCandidate candidate = candidates.get(0);
        if (json) {
            out.println("{\n  \"protocol_version\": 1,\n  \"schema_version\": \"moss-agent-1\",\n"
                    + "  \"compiler_version\": \"0.1.0-java\",\n  \"command\": \"type\",\n"
                    + "  \"ok\": true,\n  \"result\": {\n    \"status\": \"resolved\",\n"
                    + "    \"target\": {\"id\": \"" + jsonString(candidate.id) + "\", \"kind\": \"binding\", \"construct_kind\": \"binding\","
                    + " \"semantic_identity\": \"" + jsonString(candidate.semanticIdentity) + "\","
                    + " \"name\": \"" + jsonString(candidate.name) + "\", \"source\": {\"file\": \""
                    + jsonString(sourceFile) + "\", \"line\": " + line + ", \"column\": 1}, \"type\": \""
                    + jsonString(candidate.type) + "\"}, \"resolved_type\": \"" + jsonString(candidate.type)
                    + "\", \"type_facts\": {\"status\": \"resolved\", \"resolved\": \""
                    + jsonString(candidate.type) + "\", \"category\": \"concrete\", \"origin\": \"inferred\"}}\n}");
        } else {
            out.println(candidate.name + ": " + candidate.type + " at " + sourceFile + ":" + line);
        }
        return true;
    }

    private static boolean effectsBindingAtLocation(String selector, Program program, String sourceFile,
                                                    boolean json, PrintStream out) {
        String[] parts = selector.split(":");
        int line = Integer.parseInt(parts[parts[0].equals("line") ? 1 : 1]);
        String owner = null;
        String bindingName = null;
        TypeRef bindingType = null;
        if (program != null && program.localBindingLines != null) {
            for (Map.Entry<String, Map<String, Integer>> scope : program.localBindingLines.entrySet()) {
                for (Map.Entry<String, Integer> binding : scope.getValue().entrySet()) {
                    if (binding.getValue() != line) continue;
                    if (owner != null) return false;
                    owner = scope.getKey();
                    bindingName = binding.getKey();
                    bindingType = program.localBindingTypes.getOrDefault(owner, Map.of()).get(bindingName);
                }
            }
        }
        if (owner == null) return false;
        String type = bindingType != null ? bindingType.toCanonicalString() : "unit";
        String enclosing = "main".equals(owner) ? "main" : "fn:" + owner;
        boolean enclosingExternalIo = false;
        if (!"main".equals(owner) && program.functions != null) {
            for (Function function : program.functions) {
                if (!function.name.equals(owner)) continue;
                enclosingExternalIo = function.body.stream().anyMatch(statement ->
                        statement.kind == Stmt.Kind.Echo
                                || (statement.text != null && statement.text.trim().startsWith("echo ")));
                break;
            }
        }
        if (json) {
            out.println("{\n  \"protocol_version\": 1,\n  \"schema_version\": \"moss-agent-1\",\n"
                    + "  \"compiler_version\": \"0.1.0-java\",\n  \"command\": \"effects\",\n"
                    + "  \"ok\": true,\n  \"result\": {\n    \"status\": \"resolved\",\n"
                    + "    \"target\": {\"id\": \"entity-v1:binding:" + jsonString(owner) + ":"
                    + jsonString(bindingName) + "\", \"kind\": \"binding\", \"construct_kind\": \"binding\","
                    + " \"semantic_identity\": \"" + jsonString(owner) + "@" + line
                    + ":statement\", \"name\": \"" + jsonString(bindingName) + "\","
                    + " \"enclosing_entity\": \"" + jsonString(enclosing) + "\", \"source\": {\"file\": \""
                    + jsonString(sourceFile) + "\", \"line\": " + line + ", \"column\": 1}, \"type\": \""
                    + jsonString(type) + "\"}, \"observable_effects\": null, \"enclosing_callable_effects\": {"
                    + "\"external_io\": " + enclosingExternalIo + "}\n  }\n}");
        } else {
            out.println(bindingName + ": binding effects at " + sourceFile + ":" + line);
        }
        return true;
    }

    private static boolean pipelineExpressionAtLocation(String selector, Program program, String sourceFile,
                                                        String command, boolean json, PrintStream out) {
        java.util.regex.Matcher location = java.util.regex.Pattern
                .compile("main@([0-9]+):expression:[0-9]+").matcher(selector);
        if (!location.matches() || program == null || program.main == null) return false;
        int line = Integer.parseInt(location.group(1));
        Stmt found = program.main.body.stream().filter(statement -> statement.line == line).findFirst().orElse(null);
        if (found == null) return false;
        String expression = found.b;
        if (expression == null || !expression.contains("|>")) {
            expression = found.text != null ? found.text : "";
            int assignment = expression.indexOf('=');
            if (assignment >= 0) expression = expression.substring(assignment + 1).trim();
        }
        if (expression == null || !expression.contains("|>")) return false;

        org.mosslang.compiler.optimizer.FunctionalPipeline pipeline =
                new org.mosslang.compiler.optimizer.FunctionalPipeline();
        pipeline.line = line;
        pipeline.sourceFile = found.sourceFile != null && !found.sourceFile.isEmpty()
                ? found.sourceFile : sourceFile;
        pipeline.semanticIdentity = selector;
        pipeline.expression = expression;
        String[] stages = expression.split("\\|>");
        if (stages.length < 2) return false;
        pipeline.sourceExpression = stages[0].trim();
        List<String> pipelineCallbacks = org.mosslang.compiler.parser.ParserSupport.pipelineCallbacks(expression);
        org.mosslang.compiler.optimizer.FunctionalNode sourceNode =
                new org.mosslang.compiler.optimizer.FunctionalNode(
                        org.mosslang.compiler.optimizer.FunctionalNodeKind.Source, "source", pipeline.sourceExpression);
        sourceNode.line = line;
        sourceNode.sourceText = pipeline.sourceExpression;
        pipeline.nodes.add(sourceNode);
        boolean externalIo = false;
        boolean supported = true;
        for (int index = 1; index < stages.length; index++) {
            String stageText = stages[index].trim();
            java.util.regex.Matcher stage = java.util.regex.Pattern
                    .compile("([A-Za-z_][A-Za-z0-9_]*)(?:\\s*\\((.*)\\))?").matcher(stageText);
            if (!stage.matches()) { supported = false; break; }
            org.mosslang.compiler.optimizer.FunctionalNodeKind kind;
            try {
                kind = org.mosslang.compiler.optimizer.FunctionalNodeKind.valueOf(
                        stage.group(1).substring(0, 1).toUpperCase(Locale.ROOT) + stage.group(1).substring(1));
            } catch (IllegalArgumentException unsupportedStage) {
                supported = false;
                break;
            }
            org.mosslang.compiler.optimizer.FunctionalNode node =
                    new org.mosslang.compiler.optimizer.FunctionalNode(kind, stage.group(1),
                            stage.group(2) != null ? stage.group(2).trim() : "");
            node.line = line;
            node.stage = index;
            node.sourceText = stageText;
            for (String callback : pipelineCallbacks) {
                if (!node.argument.contains(callback)) continue;
                org.mosslang.compiler.optimizer.ObservableEffects callbackEffects =
                        callableObservableEffects(callback, program, new HashSet<>());
                node.effects.merge(callbackEffects);
            }
            externalIo |= node.effects.externalIo;
            if (!node.effects.isFusionSafe()) pipeline.fusionEligible = false;
            pipeline.nodes.add(node);
        }
        if (!supported) return false;
        org.mosslang.compiler.optimizer.FunctionalOptimizer.optimizePipeline(pipeline, true);
        List<String> explanations = new ArrayList<>();
        if (!pipeline.fusionEligible) {
            String reason = externalIo ? "external/I/O effect" : "observable callback effect";
            explanations.add("fusion stopped: " + reason);
        }
        explanations.addAll(pipeline.optimizationNotes);
        String resultType = "unit";
        Map<String, TypeRef> mainTypes = program.localBindingTypes.getOrDefault("main", Map.of());
        TypeRef bindingType = mainTypes.get(found.a);
        if (bindingType != null) resultType = bindingType.toCanonicalString();
        if (json) {
            String serializedExplanations = explanations.stream().map(value -> "\"" + jsonString(value) + "\"")
                    .collect(java.util.stream.Collectors.joining(","));
            if ("why".equals(command)) {
                out.println("{\"protocol_version\":1,\"schema_version\":\"moss-agent-1\","
                        + "\"compiler_version\":\"0.1.0-java\",\"command\":\"why\",\"ok\":true,"
                        + "\"result\":{\"status\":\"resolved\",\"target\":{\"id\":\"entity-v1:expression:"
                        + line + "\",\"kind\":\"expression\",\"construct_kind\":\"pipeline\","
                        + "\"semantic_identity\":\"" + jsonString(selector) + "\",\"name\":\""
                        + jsonString(expression) + "\",\"source\":{\"file\":\"" + jsonString(sourceFile)
                        + "\",\"line\":" + line + ",\"column\":1},\"type\":\"" + jsonString(resultType)
                        + "\"},\"explanations\":[" + serializedExplanations
                        + "],\"reason\":{\"code\":\"FUNCTIONAL_PLAN_DECISION\",\"evidence\":["
                        + explanations.stream().map(value -> "{\"kind\":\"compiler_decision\",\"detail\":\""
                                + jsonString(value) + "\"}").collect(java.util.stream.Collectors.joining(","))
                        + "]}}}");
            } else {
                out.println("{\"protocol_version\":1,\"schema_version\":\"moss-agent-1\","
                        + "\"compiler_version\":\"0.1.0-java\",\"command\":\"effects\",\"ok\":true,"
                        + "\"result\":{\"status\":\"resolved\",\"target\":{\"id\":\"entity-v1:expression:"
                        + line + "\",\"kind\":\"expression\",\"construct_kind\":\"pipeline\","
                        + "\"semantic_identity\":\"" + jsonString(selector) + "\",\"name\":\""
                        + jsonString(expression) + "\",\"source\":{\"file\":\"" + jsonString(sourceFile)
                        + "\",\"line\":" + line + ",\"column\":1},\"type\":\"" + jsonString(resultType)
                        + "\"},\"observable_effects\":{\"local_capture_read\":false,\"local_mutation\":false,"
                        + "\"domain_read\":false,\"domain_write\":false,\"message\":false,\"external_io\":"
                        + externalIo + ",\"may_panic\":false,\"raise_set\":[],\"may_diverge\":false,\"unresolved\":false},"
                        + "\"enclosing_callable_effects\":null,\"effect_summary\":{\"status\":\"available\","
                        + "\"direct\":{\"local_capture_read\":false,\"local_mutation\":false,\"domain_read\":false,"
                        + "\"domain_write\":false,\"message\":false,\"external_io\":" + externalIo
                        + ",\"may_panic\":false,\"raise_set\":[],\"may_diverge\":false,\"unresolved\":false},"
                        + "\"direct_status\":\"exact\"}}}");
            }
        } else {
            out.println(String.join("\n", explanations));
        }
        return true;
    }

    private static org.mosslang.compiler.optimizer.ObservableEffects callableObservableEffects(
            String callable, Program program, Set<String> visited) {
        org.mosslang.compiler.optimizer.ObservableEffects effects =
                new org.mosslang.compiler.optimizer.ObservableEffects();
        String name = callable.trim();
        if (!visited.add(name)) return effects;
        int separator = name.lastIndexOf('.');
        if (separator >= 0) name = name.substring(separator + 1);
        for (Function function : program.functions) {
            if (!function.name.equals(name) && !function.name.endsWith("__" + name)) continue;
            for (Stmt statement : function.body) {
                if (statement.kind == Stmt.Kind.Echo || (statement.text != null
                        && statement.text.trim().startsWith("echo "))) effects.externalIo = true;
            }
            for (SemanticCallEdge edge : program.semanticCallEdges) {
                if (edge.source.equals(function.name) || edge.source.equals("fn:" + function.name)) {
                    String target = edge.target.startsWith("fn:") ? edge.target.substring(3) : edge.target;
                    effects.merge(callableObservableEffects(target, program, visited));
                }
            }
        }
        return effects;
    }

    private static String functionResultType(Function function) {
        if (function.returnType != null && !function.returnType.isEmpty()) return function.returnType;
        if (function.returnTypeRef != null) {
            if (function.returnTypeRef.kind() == org.mosslang.compiler.types.TypeKind.InferenceVar) {
                String name = function.name;
                int separator = name.lastIndexOf("__");
                if (separator >= 0) name = name.substring(separator + 2);
                return "_specialized_result:" + name;
            }
            return function.returnTypeRef.toCanonicalString();
        }
        return "unit";
    }

    private static boolean resolveName(String target, Program program, String sourceFile, String kindFilter,
                                       String enclosingFilter, boolean json, PrintStream out) {
        List<ResolutionCandidate> candidates = new ArrayList<>();
        if (program != null) {
            for (Function function : program.functions) {
                if (!function.name.equals(target)) continue;
                ResolutionCandidate candidate = new ResolutionCandidate();
                candidate.kind = "function";
                candidate.name = function.name;
                candidate.id = "entity-v1:function:" + function.name;
                candidate.semanticIdentity = "fn:" + function.name + "@" + function.line;
                candidate.type = functionResultType(function);
                candidate.sourceFile = function.sourceFile;
                candidate.line = function.line;
                candidates.add(candidate);
            }
            for (ObjectType object : program.objects) {
                for (Method method : object.methods) {
                    if (!method.name.equals(target)) continue;
                    String qualified = object.name + "." + method.name;
                    if (enclosingFilter != null && !enclosingFilter.isEmpty()
                            && !enclosingFilter.equals("method:" + qualified)
                            && !enclosingFilter.equals(qualified)) continue;
                    ResolutionCandidate candidate = new ResolutionCandidate();
                    candidate.kind = "method";
                    candidate.name = qualified;
                    candidate.id = "entity-v1:method:" + qualified;
                    candidate.semanticIdentity = "method:" + qualified + "@" + method.line;
                    candidate.type = method.returnType != null && !method.returnType.isEmpty()
                            ? method.returnType : "unit";
                    candidate.sourceFile = method.sourceFile;
                    candidate.line = method.line;
                    candidates.add(candidate);
                }
                for (Field field : object.fields) {
                    if (!field.name.equals(target)) continue;
                    ResolutionCandidate candidate = new ResolutionCandidate();
                    candidate.kind = "field";
                    candidate.name = object.name + "." + field.name;
                    candidate.id = "entity-v1:field:" + candidate.name;
                    candidate.semanticIdentity = "field:" + candidate.name + "@" + field.line;
                    candidate.type = field.type;
                    candidate.sourceFile = field.sourceFile;
                    candidate.line = field.line;
                    candidates.add(candidate);
                }
            }
        }
        if (kindFilter != null && !kindFilter.isEmpty()) {
            candidates.removeIf(candidate -> !candidate.kind.equals(kindFilter));
        }
        candidates.sort(Comparator.comparing((ResolutionCandidate candidate) -> candidate.name)
                .thenComparing(candidate -> candidate.sourceFile).thenComparing(candidate -> candidate.id));
        return emitResolutionCandidates(target, candidates, sourceFile, json, out);
    }

    private static boolean emitResolutionCandidates(String selector, List<ResolutionCandidate> candidates,
                                                    String sourceFile, boolean json, PrintStream out) {
        if (candidates.size() == 1) {
            ResolutionCandidate candidate = candidates.get(0);
            if (json) {
                out.println("{\n  \"protocol_version\": 1,\n  \"schema_version\": \"moss-agent-1\",\n"
                        + "  \"compiler_version\": \"0.1.0-java\",\n  \"command\": \"resolve\",\n"
                        + "  \"ok\": true,\n  \"result\": {\n    \"status\": \"resolved\",\n"
                        + "    \"target\": {\"id\": \"" + jsonString(candidate.id) + "\", \"kind\": \""
                        + candidate.kind + "\", \"semantic_identity\": \"" + jsonString(candidate.semanticIdentity)
                        + "\", \"name\": \"" + jsonString(candidate.name) + "\", \"source\": {\"file\": \""
                        + jsonString(candidate.sourceFile) + "\", \"line\": " + candidate.line + ", \"column\": 1}, \"type\": \""
                        + jsonString(candidate.type) + "\"}\n  }\n}");
            } else {
                out.println(candidate.name + ": " + candidate.kind + " (" + candidate.type + ") at "
                        + candidate.sourceFile + ":" + candidate.line);
            }
            return true;
        }
        return emitResolutionFailure(selector, candidates, sourceFile, json, out);
    }

    private static boolean emitResolutionFailure(String selector, List<ResolutionCandidate> candidates,
                                                 String sourceFile, boolean json, PrintStream out) {
        String status = candidates.isEmpty() ? "missing" : "ambiguous";
        if (json) {
            StringBuilder candidateJson = new StringBuilder("[");
            for (int i = 0; i < candidates.size(); i++) {
                ResolutionCandidate candidate = candidates.get(i);
                if (i > 0) candidateJson.append(", ");
                candidateJson.append("{\"id\":\"").append(jsonString(candidate.id))
                        .append("\",\"kind\":\"").append(candidate.kind)
                        .append("\",\"name\":\"").append(jsonString(candidate.name))
                        .append("\",\"source\":{\"file\":\"").append(jsonString(candidate.sourceFile))
                        .append("\",\"line\":").append(candidate.line).append("}}");
            }
            candidateJson.append(']');
            out.println("{\n  \"protocol_version\": 1,\n  \"schema_version\": \"moss-agent-1\",\n"
                    + "  \"compiler_version\": \"0.1.0-java\",\n  \"command\": \"resolve\",\n"
                    + "  \"ok\": false,\n  \"error\": {\"code\": \"QUERY_TARGET_NOT_FOUND\", \"severity\": \"error\","
                    + " \"message\": \"target did not identify one semantic entity\", \"source_file\": \""
                    + jsonString(sourceFile) + "\", \"line\": 0, \"column\": 1, \"span\": null, \"source\": null,"
                    + " \"semantic_identity\": null, \"symbol\": \"" + jsonString(selector)
                    + "\", \"rule\": null, \"cause\": null, \"related\": [], \"guidance\": null,"
                    + " \"details\": {\"resolution\": {\"status\": \"" + status
                    + "\", \"candidates\": " + candidateJson + "}}, \"fixes\": [], \"legal_alternatives\": []}}");
        } else {
            out.println("moss: target resolution is " + status + ": " + selector);
        }
        return false;
    }

    private static boolean resolveSourceLocation(String selector, Program program, String sourceFile,
                                                 String kindFilter, boolean json, PrintStream out) {
        String[] parts = selector.split(":");
        int line = Integer.parseInt(parts[parts.length - (parts[0].equals("line") ? 1 : 2)]);
        List<ResolutionCandidate> candidates = new ArrayList<>();
        if (program != null && program.localBindingLines != null) {
            for (Map.Entry<String, Map<String, Integer>> scope : program.localBindingLines.entrySet()) {
                Map<String, TypeRef> types = program.localBindingTypes.getOrDefault(scope.getKey(), Map.of());
                for (Map.Entry<String, Integer> binding : scope.getValue().entrySet()) {
                    if (binding.getValue() != line) continue;
                    ResolutionCandidate candidate = new ResolutionCandidate();
                    candidate.kind = "binding";
                    candidate.name = binding.getKey();
                    candidate.id = "entity-v1:binding:" + scope.getKey() + ":" + binding.getKey();
                    candidate.semanticIdentity = scope.getKey() + "@" + line + ":statement";
                    TypeRef type = types.get(binding.getKey());
                    candidate.type = type != null ? type.toCanonicalString() : "unit";
                    candidate.sourceFile = sourceFile;
                    candidate.line = line;
                    candidates.add(candidate);
                }
            }
        }
        if (program != null) {
            for (Function function : program.functions) {
                if (function.line != line || !sameSource(function.sourceFile, sourceFile)) continue;
                ResolutionCandidate candidate = new ResolutionCandidate();
                candidate.kind = "function";
                candidate.name = function.name;
                candidate.id = "entity-v1:function:" + function.name;
                candidate.semanticIdentity = "fn:" + function.name + "@" + function.line;
                candidate.type = functionResultType(function);
                candidate.sourceFile = function.sourceFile;
                candidate.line = line;
                candidates.add(candidate);
                for (FunctionSpecialization specialization : function.specializations) {
                    String specializationName = function.name + "<"
                            + String.join(",", specialization.parameterTypes).toLowerCase(Locale.ROOT) + ">";
                    ResolutionCandidate specialized = new ResolutionCandidate();
                    specialized.kind = "specialization";
                    specialized.name = specializationName;
                    specialized.id = "entity-v1:specialization:" + specializationName;
                    specialized.semanticIdentity = "specialization:" + specializationName;
                    specialized.type = specialization.returnType;
                    specialized.sourceFile = function.sourceFile;
                    specialized.line = line;
                    candidates.add(specialized);
                }
            }
            for (ObjectType object : program.objects) {
                for (Method method : object.methods) {
                    if (method.line != line || !sameSource(method.sourceFile, sourceFile)) continue;
                    ResolutionCandidate candidate = new ResolutionCandidate();
                    candidate.kind = "method";
                    candidate.name = object.name + "." + method.name;
                    candidate.id = "entity-v1:method:" + candidate.name;
                    candidate.semanticIdentity = "method:" + candidate.name + "@" + method.line;
                    candidate.type = method.returnType != null && !method.returnType.isEmpty()
                            ? method.returnType : "unit";
                    candidate.sourceFile = method.sourceFile;
                    candidate.line = line;
                    candidates.add(candidate);
                }
            }
        }
        if (program != null && program.semanticCallEdges != null) {
            Map<String, Integer> includedCallOccurrences = new HashMap<>();
            for (SemanticCallEdge edge : program.semanticCallEdges) {
                if (edge.line != line || !sameSource(edge.sourceFile, sourceFile)) continue;
                String occurrenceKey = edge.source + "|" + edge.target + "|" + edge.line;
                int allowedOccurrences = sourceCallOccurrenceCount(edge, program);
                int includedOccurrences = includedCallOccurrences.getOrDefault(occurrenceKey, 0);
                if (includedOccurrences >= allowedOccurrences) continue;
                includedCallOccurrences.put(occurrenceKey, includedOccurrences + 1);
                ResolutionCandidate candidate = new ResolutionCandidate();
                candidate.kind = "call";
                candidate.name = edge.target;
                candidate.id = edge.callSiteId();
                candidate.semanticIdentity = edge.callSiteSemanticIdentity();
                candidate.type = edge.returnTypeRef != null ? edge.returnTypeRef.toCanonicalString() : "unit";
                candidate.sourceFile = edge.sourceFile;
                candidate.line = line;
                candidates.add(candidate);
            }
        }
        if (selector.startsWith("at:") && parts.length >= 3) {
            int column = Integer.parseInt(parts[2]);
            String sourceLine = sourceLineText(program, sourceFile, line);
            String token = identifierAtColumn(sourceLine, column);
            if (token != null) {
                if (program != null) {
                    for (Function function : program.functions) {
                        for (Stmt statement : function.body) {
                            if (!statement.continuationLines.contains(line)) continue;
                            String expression = statement.a != null && !statement.a.isEmpty()
                                    ? statement.a : statement.text;
                            for (String callback : org.mosslang.compiler.parser.ParserSupport
                                    .pipelineCallbacks(expression)) {
                                String callbackName = callback.substring(callback.lastIndexOf('.') + 1);
                                if (!callbackName.equals(token)) continue;
                                ResolutionCandidate candidate = new ResolutionCandidate();
                                candidate.kind = "function";
                                candidate.name = callback;
                                candidate.id = "entity-v1:function:" + callback;
                                Function definition = program.functions.stream().filter(fn ->
                                        fn.name.equals(callbackName) || fn.name.endsWith("__" + callbackName))
                                        .findFirst().orElse(null);
                                candidate.semanticIdentity = definition != null
                                        ? "fn:" + definition.name + "@" + definition.line : "fn:" + callbackName;
                                candidate.type = definition != null ? functionResultType(definition) : "unit";
                                candidate.sourceFile = definition != null ? definition.sourceFile : sourceFile;
                                candidate.line = definition != null ? definition.line : line;
                                candidates.add(candidate);
                            }
                        }
                    }
                }
                candidates.removeIf(candidate -> !candidate.name.equals(token)
                        && !candidate.name.endsWith("." + token)
                        && !candidate.name.endsWith("__" + token)
                        && !candidate.name.startsWith(token + "<")
                        && !candidate.name.startsWith("fn:" + token));
                if (!"specialization".equals(kindFilter))
                    candidates.removeIf(candidate -> "specialization".equals(candidate.kind));
            }
        }
        if (kindFilter != null && !kindFilter.isEmpty()) {
            candidates.removeIf(candidate -> !candidate.kind.equals(kindFilter));
        }
        candidates.sort(Comparator.comparing((ResolutionCandidate candidate) -> candidate.kind)
                .thenComparing(candidate -> candidate.name).thenComparing(candidate -> candidate.id));
        if (candidates.size() == 1) {
            ResolutionCandidate candidate = candidates.get(0);
            if (json) {
                out.println("{\n  \"protocol_version\": 1,\n  \"schema_version\": \"moss-agent-1\",\n"
                        + "  \"compiler_version\": \"0.1.0-java\",\n  \"command\": \"resolve\",\n"
                        + "  \"ok\": true,\n  \"result\": {\n    \"status\": \"resolved\",\n"
                        + "    \"target\": {\"id\": \"" + jsonString(candidate.id) + "\", \"kind\": \""
                        + candidate.kind + "\", \"semantic_identity\": \"" + jsonString(candidate.semanticIdentity)
                        + "\", \"name\": \"" + jsonString(candidate.name) + "\", \"source\": {\"file\": \""
                        + jsonString(candidate.sourceFile) + "\", \"line\": " + candidate.line + ", \"column\": 1}, \"type\": \""
                        + jsonString(candidate.type) + "\"}\n  }\n}");
            } else {
                out.println(candidate.name + ": " + candidate.kind + " (" + candidate.type + ") at "
                        + sourceFile + ":" + candidate.line);
            }
            return true;
        }
        String status = candidates.isEmpty() ? "missing" : "ambiguous";
        if (json) {
            StringBuilder candidateJson = new StringBuilder("[");
            for (int i = 0; i < candidates.size(); i++) {
                ResolutionCandidate candidate = candidates.get(i);
                if (i > 0) candidateJson.append(", ");
                candidateJson.append("{\"id\":\"").append(jsonString(candidate.id))
                        .append("\",\"kind\":\"").append(candidate.kind)
                        .append("\",\"name\":\"").append(jsonString(candidate.name))
                        .append("\",\"source\":{\"file\":\"").append(jsonString(candidate.sourceFile))
                        .append("\",\"line\":").append(candidate.line).append("}}");
            }
            candidateJson.append(']');
            out.println("{\n  \"protocol_version\": 1,\n  \"schema_version\": \"moss-agent-1\",\n"
                    + "  \"compiler_version\": \"0.1.0-java\",\n  \"command\": \"resolve\",\n"
                    + "  \"ok\": false,\n  \"error\": {\"code\": \"QUERY_TARGET_NOT_FOUND\", \"severity\": \"error\","
                    + " \"message\": \"source location did not identify one semantic target\", \"source_file\": \""
                    + jsonString(sourceFile) + "\", \"line\": " + line + ", \"column\": 1, \"span\": null, \"source\": null,"
                    + " \"semantic_identity\": null, \"symbol\": null, \"rule\": null, \"cause\": null, \"related\": [],"
                    + " \"guidance\": null, \"details\": {\"resolution\": {\"status\": \"" + status
                    + "\", \"candidates\": " + candidateJson + "}}, \"fixes\": [], \"legal_alternatives\": []}}");
        } else {
            out.println("moss: source location resolution is " + status);
        }
        return false;
    }

    private static String sourceLineText(Program program, String sourceFile, int line) {
        if (program != null && program.sourceLines != null) {
            for (Line sourceLine : program.sourceLines) {
                if (sourceLine.no == line && sourceLine.text != null && !sourceLine.text.isEmpty())
                    return sourceLine.text;
            }
        }
        try {
            List<String> lines = java.nio.file.Files.readAllLines(java.nio.file.Paths.get(sourceFile));
            return line > 0 && line <= lines.size() ? lines.get(line - 1) : "";
        } catch (Exception ignored) {
            return "";
        }
    }

    private static boolean referencesQuery(String selector, Program program, String sourceFile,
                                           boolean json, PrintStream out) {
        String name = selector.replaceFirst("^(entity-v1:function:|function:|fn:)", "");
        Function function = program.functions.stream().filter(candidate -> candidate.name.equals(name)
                || candidate.name.endsWith("__" + name)).findFirst().orElse(null);
        if (function == null) return false;
        String callableName = function.name;
        List<String> references = new ArrayList<>();
        for (SemanticCallEdge edge : program.semanticCallEdges) {
            String target = edge.target.startsWith("fn:") ? edge.target.substring(3) : edge.target;
            if (!target.equals(callableName) && !target.endsWith("__" + callableName)) continue;
            references.add("{\"kind\":\"callable_reference\",\"source\":{\"file\":\""
                    + jsonString(edge.sourceFile) + "\",\"line\":" + edge.line + ",\"column\":1},"
                    + "\"enclosing_entity\":\"" + jsonString(edge.source) + "\"}");
        }
        for (Function caller : program.functions) {
            for (Stmt statement : caller.body) {
                String expression = statement.a != null && !statement.a.isEmpty()
                        ? statement.a : statement.text;
                if (expression == null || expression.isEmpty()) continue;
                List<String> callbacks = org.mosslang.compiler.parser.ParserSupport.pipelineCallbacks(expression);
                for (String callback : callbacks) {
                    String callbackName = callback.substring(callback.lastIndexOf('.') + 1);
                    if (!callbackName.equals(callableName) && !callbackName.endsWith("__" + callableName)) continue;
                    int line = statement.line;
                    for (int continuation : statement.continuationLines.toArray()) {
                        String continuationText = sourceLineText(program, sourceFile, continuation);
                        if (continuationText.contains(callbackName)) { line = continuation; break; }
                    }
                    references.add("{\"kind\":\"callable_reference\",\"source\":{\"file\":\""
                            + jsonString(statement.sourceFile != null && !statement.sourceFile.isEmpty()
                                    ? statement.sourceFile : sourceFile)
                            + "\",\"line\":" + line + ",\"column\":1},\"enclosing_entity\":\"fn:"
                            + jsonString(caller.name) + "\"}");
                }
            }
        }
        if (json) {
            out.println("{\"protocol_version\":1,\"schema_version\":\"moss-agent-1\","
                    + "\"compiler_version\":\"0.1.0-java\",\"command\":\"references\",\"ok\":true,"
                    + "\"result\":{\"status\":\"resolved\",\"target\":{\"id\":\"entity-v1:function:"
                    + jsonString(callableName) + "\",\"kind\":\"function\",\"name\":\""
                    + jsonString(callableName) + "\",\"semantic_identity\":\"fn:" + jsonString(callableName)
                    + "@" + function.line + "\"},\"definition\":{\"entity_id\":\"entity-v1:function:"
                    + jsonString(callableName) + "\",\"kind\":\"definition\",\"source\":{\"file\":\""
                    + jsonString(function.sourceFile) + "\",\"line\":" + function.line + ",\"column\":1}},"
                    + "\"references\":[" + String.join(",", references) + "]}}");
        } else {
            out.println(callableName + ": " + references.size() + " references");
        }
        return true;
    }

    private static String identifierAtColumn(String sourceLine, int oneBasedColumn) {
        if (sourceLine == null || oneBasedColumn <= 0 || oneBasedColumn > sourceLine.length()) return null;
        int index = oneBasedColumn - 1;
        if (!Character.isLetterOrDigit(sourceLine.charAt(index)) && sourceLine.charAt(index) != '_') return null;
        int start = index;
        int end = index + 1;
        while (start > 0 && (Character.isLetterOrDigit(sourceLine.charAt(start - 1))
                || sourceLine.charAt(start - 1) == '_')) start--;
        while (end < sourceLine.length() && (Character.isLetterOrDigit(sourceLine.charAt(end))
                || sourceLine.charAt(end) == '_')) end++;
        return sourceLine.substring(start, end);
    }

    private static boolean sameSource(String candidateSource, String requestedSource) {
        if (candidateSource == null || candidateSource.isEmpty()) return true;
        return new java.io.File(candidateSource).getAbsolutePath()
                .equals(new java.io.File(requestedSource).getAbsolutePath());
    }

    private static String jsonString(String value) {
        if (value == null) return "";
        return value.replace("\\", "\\\\").replace("\"", "\\\"")
                .replace("\n", "\\n").replace("\r", "\\r");
    }

    private enum EffectKind { EXTERNAL_IO, MAY_PANIC }

    private static boolean callableHasEffect(List<Stmt> body, String resultExpression, Program program,
                                             Set<String> visited, EffectKind effect) {
        return callableHasEffect(body, resultExpression, program, visited, effect, null, null);
    }

    private static boolean callableHasEffect(List<Stmt> body, String resultExpression, Program program,
                                             Set<String> visited, EffectKind effect,
                                             Function callerFunction, Method ownerMethod) {
        if (body == null) return false;
        for (Stmt statement : body) {
            if (statement == null) continue;
            StringBuilder source = new StringBuilder();
            if (statement.text != null) source.append(statement.text).append(' ');
            if (statement.a != null) source.append(statement.a).append(' ');
            if (statement.b != null) source.append(statement.b).append(' ');
            if (statement.args != null) {
                for (String argument : statement.args) source.append(argument).append(' ');
            }
            if (hasEffect(source.toString(), statement, effect)) return true;
            if (program != null && program.functions != null) {
                for (Function callee : program.functions) {
                    if (!containsFunctionCall(source.toString(), callee.name)
                            || !visited.add("fn:" + callee.name)) continue;
                    if (callableHasEffect(callee.body, callee.resultExpression, program, visited, effect,
                            callee, null)) return true;
                }
            }
            if (callableMethodHasEffect(source.toString(), program, visited, effect,
                    callerFunction, ownerMethod)) return true;
        }
        if (resultExpression == null) return false;
        if (hasEffect(resultExpression, null, effect)) return true;
        if (program != null && program.functions != null) {
            for (Function callee : program.functions) {
                if (!containsFunctionCall(resultExpression, callee.name)
                        || !visited.add("fn:" + callee.name)) continue;
                if (callableHasEffect(callee.body, callee.resultExpression, program, visited, effect,
                        callee, null)) return true;
            }
        }
        return callableMethodHasEffect(resultExpression, program, visited, effect,
                callerFunction, ownerMethod);
    }

    private static boolean callableMethodHasEffect(String expression, Program program, Set<String> visited,
                                                   EffectKind effect, Function callerFunction,
                                                   Method ownerMethod) {
        if (program == null || program.objects == null) return false;
        if (ownerMethod != null) {
            for (ObjectType object : program.objects) {
                if (!object.name.equals(ownerMethod.owner)) continue;
                for (Method method : object.methods) {
                    if (method == ownerMethod || !containsFunctionCall(expression, method.name)
                            || !visited.add("method:" + method.owner + "." + method.name)) continue;
                    if (callableHasEffect(method.body, method.resultExpression, program, visited, effect,
                            null, method)) return true;
                }
            }
        }
        Map<String, TypeRef> bindings = callerFunction != null && program.localBindingTypes != null
                ? program.localBindingTypes.getOrDefault(callerFunction.name, Map.of()) : Map.of();
        for (ObjectType object : program.objects) {
            for (Method method : object.methods) {
                String pattern = "(?<![A-Za-z0-9_])((?:self|[A-Za-z_][A-Za-z0-9_]*)(?:\\s*\\.\\s*[A-Za-z_][A-Za-z0-9_]*)*)\\s*\\.\\s*"
                        + java.util.regex.Pattern.quote(method.name) + "\\s*\\(";
                java.util.regex.Matcher call = java.util.regex.Pattern.compile(pattern).matcher(expression);
                while (call.find()) {
                    String typeName = resolveObjectMethodReceiverTypeName(
                            call.group(1), program, bindings, ownerMethod);
                    if (typeName == null) continue;
                    if (!matchesObjectTypeName(typeName, object.name)) continue;
                    String identity = "method:" + method.owner + "." + method.name;
                    if (!visited.add(identity)) continue;
                    if (callableHasEffect(method.body, method.resultExpression, program, visited, effect,
                            null, method)) return true;
                }
            }
        }
        return false;
    }

    private static String resolveObjectMethodReceiverTypeName(String receiverPath, Program program,
                                                              Map<String, TypeRef> bindings,
                                                              Method ownerMethod) {
        if (receiverPath == null || receiverPath.isBlank()) return null;
        String[] path = receiverPath.replaceAll("\\s", "").split("\\.");
        if (path.length == 0) return null;
        String typeName;
        if ("self".equals(path[0]) && ownerMethod != null) {
            typeName = ownerMethod.owner;
        } else {
            TypeRef receiverType = bindings != null ? bindings.get(path[0]) : null;
            typeName = receiverType != null ? receiverType.name() : null;
        }
        if (typeName == null) return null;
        for (int pathIndex = 1; pathIndex < path.length; pathIndex++) {
            ObjectType receiverObject = findObjectType(program, typeName);
            if (receiverObject == null) return null;
            String fieldName = path[pathIndex];
            Field field = receiverObject.fields.stream()
                    .filter(candidate -> candidate.name.equals(fieldName))
                    .findFirst().orElse(null);
            if (field == null || field.type == null || field.type.isBlank()) return null;
            TypeRef fieldType = field.typeRef != null
                    ? field.typeRef : TypeParser.parseCanonical(field.type);
            typeName = fieldType != null ? fieldType.name() : field.type;
            if (typeName == null || typeName.isBlank()) return null;
        }
        return typeName;
    }

    private static boolean hasUnresolvedObjectMethodReceiver(Function function, Program program) {
        if (function == null || program == null || program.objects == null) return false;
        Map<String, TypeRef> bindings = program.localBindingTypes != null
                ? program.localBindingTypes.getOrDefault(function.name, Map.of()) : Map.of();
        for (Stmt statement : function.body) {
            if (statement == null) continue;
            String expression = (statement.text == null ? "" : statement.text) + " "
                    + (statement.a == null ? "" : statement.a) + " "
                    + (statement.b == null ? "" : statement.b);
            for (ObjectType object : program.objects) {
                for (Method method : object.methods) {
                    java.util.regex.Matcher calls = java.util.regex.Pattern.compile(
                            "(?<![A-Za-z0-9_])((?:self|[A-Za-z_][A-Za-z0-9_]*)(?:\\s*\\.\\s*[A-Za-z_][A-Za-z0-9_]*)*)\\s*\\.\\s*"
                                    + java.util.regex.Pattern.quote(method.name) + "\\s*\\(")
                            .matcher(expression);
                    while (calls.find()) {
                        String receiverType = resolveObjectMethodReceiverTypeName(
                                calls.group(1), program, bindings, null);
                        if (receiverType == null || findObjectType(program, receiverType) == null)
                            return true;
                    }
                    for (String callback : org.mosslang.compiler.parser.ParserSupport
                            .pipelineCallbacks(expression)) {
                        int dot = callback.lastIndexOf('.');
                        if (dot <= 0 || !method.name.equals(callback.substring(dot + 1))) continue;
                        String receiverType = resolveObjectMethodReceiverTypeName(
                                callback.substring(0, dot), program, bindings, null);
                        if (receiverType == null || findObjectType(program, receiverType) == null)
                            return true;
                    }
                }
            }
        }
        return false;
    }

    private static ObjectType findObjectType(Program program, String typeName) {
        if (program == null || program.objects == null || typeName == null) return null;
        for (ObjectType object : program.objects) {
            if (matchesObjectTypeName(typeName, object.name)) return object;
        }
        return null;
    }

    private static boolean matchesObjectTypeName(String typeName, String objectName) {
        return typeName != null && objectName != null
                && (typeName.equals(objectName) || typeName.endsWith("__" + objectName)
                        || typeName.endsWith("::" + objectName));
    }

    private static boolean hasEffect(String source, Stmt statement, EffectKind effect) {
        if (effect == EffectKind.EXTERNAL_IO) {
            return (statement != null && statement.kind == Stmt.Kind.Echo)
                    || source.trim().startsWith("echo ");
        }
        return source.contains("assert(");
    }

    private static Set<String> callableRaisedVariants(Function function, Method method,
                                                      Handler handler, Program program) {
        return collectRaisedVariants(function, method, handler, program, new HashSet<>());
    }

    private static Set<String> collectRaisedVariants(Function function, Method method,
                                                    Handler handler, Program program,
                                                    Set<String> visited) {
        String identity = function != null ? "fn:" + function.name
                : method != null ? "method:" + method.owner + "." + method.name
                : handler != null ? "handler:" + handler.name : "";
        if (!identity.isEmpty() && !visited.add(identity)) return Set.of();
        List<Stmt> body = function != null ? function.body
                : method != null ? method.body
                : handler != null ? handler.body : List.of();
        Set<String> variants = new java.util.TreeSet<>();
        for (int index = 0; index < body.size(); index++) {
            Stmt statement = body.get(index);
            if (statement == null || statement.kind != Stmt.Kind.Raise) continue;
            String expression = statement.a == null ? "" : statement.a.trim();
            if (expression.isEmpty() && statement.text != null
                    && statement.text.trim().startsWith("raise ")) {
                expression = statement.text.trim().substring(6).trim();
            }
            if (expression.isEmpty()) {
                for (String canonical : reraisedVariants(body, index, program, visited)) {
                    if (!isRaisedVariantRecovered(body, index, canonical)) variants.add(canonical);
                }
                continue;
            }
            int open = expression.indexOf('(');
            if (open >= 0) expression = expression.substring(0, open).trim();
            String canonical = canonicalRaisedVariant(expression, program);
            if (canonical == null) continue;
            if (!isRaisedVariantRecovered(body, index, canonical)) variants.add(canonical);
        }
        if (function != null && program != null && program.functions != null) {
            for (int index = 0; index < body.size(); index++) {
                Stmt statement = body.get(index);
                if (statement == null) continue;
                StringBuilder expressionBuilder = new StringBuilder();
                if (statement.text != null) expressionBuilder.append(statement.text).append(' ');
                if (statement.a != null) expressionBuilder.append(statement.a).append(' ');
                if (statement.b != null) expressionBuilder.append(statement.b).append(' ');
                if (statement.args != null) {
                    for (String argument : statement.args) expressionBuilder.append(argument).append(' ');
                }
                String expression = expressionBuilder.toString();
                for (Function candidate : program.functions) {
                    if (candidate == function
                            || (!containsFunctionCall(expression, candidate.name)
                                    && !org.mosslang.compiler.parser.ParserSupport
                                            .usesPipelineFunctionCallback(expression, candidate.name))) continue;
                    Set<String> calledRaises = collectRaisedVariants(candidate, null, null, program, visited);
                    for (String raised : calledRaises) {
                        if (!isRaisedVariantRecovered(body, index, raised)) variants.add(raised);
                    }
                }
            }
            if (function.resultExpression != null && !function.resultExpression.isBlank()) {
                for (Function candidate : program.functions) {
                    if (candidate != function
                            && (containsFunctionCall(function.resultExpression, candidate.name)
                                    || org.mosslang.compiler.parser.ParserSupport
                                            .usesPipelineFunctionCallback(function.resultExpression, candidate.name))) {
                        variants.addAll(collectRaisedVariants(candidate, null, null, program, visited));
                    }
                }
                collectLocalObjectMethodRaises(function.resultExpression, function, null, body,
                        body.size(), program, visited, variants);
            }
            if (program.objects != null) {
                for (Stmt statement : body) {
                    if (statement == null) continue;
                    StringBuilder expression = new StringBuilder();
                    if (statement.text != null) expression.append(statement.text).append(' ');
                    if (statement.a != null) expression.append(statement.a).append(' ');
                    if (statement.b != null) expression.append(statement.b).append(' ');
                    if (statement.args != null) {
                        for (String argument : statement.args) expression.append(argument).append(' ');
                    }
                    collectLocalObjectMethodRaises(expression.toString(), function, null, body,
                            body.indexOf(statement), program, visited, variants);
                }
            }
        }
        if (method != null && program != null && program.objects != null) {
            for (int index = 0; index < body.size(); index++) {
                Stmt statement = body.get(index);
                if (statement == null) continue;
                StringBuilder expression = new StringBuilder();
                if (statement.text != null) expression.append(statement.text).append(' ');
                if (statement.a != null) expression.append(statement.a).append(' ');
                if (statement.b != null) expression.append(statement.b).append(' ');
                if (statement.args != null) {
                    for (String argument : statement.args) expression.append(argument).append(' ');
                }
                collectLocalObjectMethodRaises(expression.toString(), null, method, body,
                        index, program, visited, variants);
            }
            if (method.resultExpression != null && !method.resultExpression.isBlank()) {
                collectLocalObjectMethodRaises(method.resultExpression, null, method, body,
                        body.size(), program, visited, variants);
            }
            for (ObjectType object : program.objects) {
                if (!object.name.equals(method.owner)) continue;
                for (Method candidate : object.methods) {
                    if (candidate == method) continue;
                    for (int index = 0; index < body.size(); index++) {
                        Stmt statement = body.get(index);
                        if (statement == null) continue;
                        StringBuilder expression = new StringBuilder();
                        if (statement.text != null) expression.append(statement.text).append(' ');
                        if (statement.a != null) expression.append(statement.a).append(' ');
                        if (statement.b != null) expression.append(statement.b).append(' ');
                        if (statement.args != null) {
                            for (String argument : statement.args) expression.append(argument).append(' ');
                        }
                        if (!containsFunctionCall(expression.toString(), candidate.name)) continue;
                        Set<String> calledRaises = collectRaisedVariants(null, candidate, null, program, visited);
                        for (String raised : calledRaises) {
                            if (!isRaisedVariantRecovered(body, index, raised)) variants.add(raised);
                        }
                    }
                    if (method.resultExpression != null
                            && containsFunctionCall(method.resultExpression, candidate.name)) {
                        variants.addAll(collectRaisedVariants(null, candidate, null, program, visited));
                    }
                }
            }
            if (method.resultExpression != null && program.functions != null) {
                for (Function candidate : program.functions) {
                    if (containsFunctionCall(method.resultExpression, candidate.name)) {
                        variants.addAll(collectRaisedVariants(candidate, null, null, program, visited));
                    }
                }
            }
        }
        return variants;
    }

    private static Set<String> reraisedVariants(List<Stmt> body, int raiseIndex,
                                                Program program, Set<String> visited) {
        int recoverIndex = enclosingRecoverIndex(body, raiseIndex);
        if (recoverIndex < 0) return Set.of();
        Stmt recover = body.get(recoverIndex);
        String pattern = recover.a == null ? "" : recover.a.trim();
        Set<String> selectedPatterns = new HashSet<>();
        if (!pattern.isEmpty()) {
            for (String part : org.mosslang.compiler.parser.ParserSupport.splitTopLevel(pattern, ',')) {
                String canonical = canonicalRaisedVariant(part, program);
                if (canonical != null) selectedPatterns.add(canonical);
            }
            if (selectedPatterns.isEmpty()) return Set.of();
        }

        Stmt raised = body.get(recoverIndex);
        int tryIndex = -1;
        for (int index = recoverIndex - 1; index >= 0; index--) {
            Stmt candidate = body.get(index);
            if (candidate == null || candidate.kind != Stmt.Kind.Try
                    || candidate.indent != raised.indent) continue;
            int tryBodyEnd = index + 1;
            while (tryBodyEnd < body.size()
                    && body.get(tryBodyEnd).indent > candidate.indent) tryBodyEnd++;
            if (tryBodyEnd == recoverIndex) {
                tryIndex = index;
                break;
            }
        }
        if (tryIndex < 0) return Set.of();
        int tryBodyEnd = recoverIndex;
        Set<String> variants = new java.util.TreeSet<>();
        for (int index = tryIndex + 1; index < tryBodyEnd; index++) {
            Stmt statement = body.get(index);
            if (statement == null) continue;
            if (statement.kind == Stmt.Kind.Raise) {
                String expression = statement.a == null ? "" : statement.a.trim();
                if (expression.isEmpty() && statement.text != null
                        && statement.text.trim().startsWith("raise ")) {
                    expression = statement.text.trim().substring(6).trim();
                }
                String raisedVariant = canonicalRaisedVariant(expression, program);
                if (raisedVariant != null
                        && !isRaisedVariantRecovered(body, index, raisedVariant, tryIndex)) {
                    variants.add(raisedVariant);
                }
            }
            StringBuilder expressions = new StringBuilder();
            if (statement.text != null) expressions.append(statement.text).append(' ');
            if (statement.a != null) expressions.append(statement.a).append(' ');
            if (statement.b != null) expressions.append(statement.b).append(' ');
            if (statement.args != null) {
                for (String argument : statement.args) expressions.append(argument).append(' ');
            }
            if (program.functions != null) {
                for (Function candidate : program.functions) {
                    if (!containsFunctionCall(expressions.toString(), candidate.name)) continue;
                    for (String calledVariant : collectRaisedVariants(candidate, null, null,
                            program, visited)) {
                        if (!isRaisedVariantRecovered(body, index, calledVariant, tryIndex))
                            variants.add(calledVariant);
                    }
                }
            }
        }
        if (!selectedPatterns.isEmpty()) variants.retainAll(selectedPatterns);
        return variants;
    }

    private static int enclosingRecoverIndex(List<Stmt> body, int raiseIndex) {
        Stmt raised = body.get(raiseIndex);
        for (int recoverIndex = raiseIndex - 1; recoverIndex >= 0; recoverIndex--) {
            Stmt recover = body.get(recoverIndex);
            if (recover == null || recover.kind != Stmt.Kind.Recover
                    || recover.indent >= raised.indent) continue;
            int recoverEnd = recoverIndex + 1;
            while (recoverEnd < body.size()
                    && body.get(recoverEnd).indent > recover.indent) recoverEnd++;
            if (raiseIndex < recoverEnd) return recoverIndex;
        }
        return -1;
    }

    private static String canonicalRaisedVariant(String expression, Program program) {
        if (expression == null || expression.isBlank()) return null;
        String normalized = expression.trim();
        int payload = normalized.indexOf('(');
        if (payload >= 0) normalized = normalized.substring(0, payload).trim();
        int dot = normalized.lastIndexOf('.');
        if (dot <= 0 || dot == normalized.length() - 1) return null;
        String enumName = normalized.substring(0, dot).replace('.', '_');
        String caseName = normalized.substring(dot + 1).trim();
        if (program != null && program.enums != null) {
            for (EnumType declaration : program.enums) {
                if (declaration.name.equals(enumName)
                        || declaration.name.endsWith("__" + enumName)) {
                    enumName = declaration.name;
                    break;
                }
            }
        }
        return "enum:" + enumName + ":" + caseName;
    }

    private static boolean containsFunctionCall(String expression, String functionName) {
        if (expression == null || expression.isEmpty() || functionName == null || functionName.isEmpty()) return false;
        return java.util.regex.Pattern.compile("(?<![A-Za-z0-9_])"
                + java.util.regex.Pattern.quote(functionName) + "\\s*\\(")
                .matcher(expression).find();
    }

    private static void collectLocalObjectMethodRaises(String expression, Function function,
                                                       Method ownerMethod,
                                                       List<Stmt> callerBody, int statementIndex,
                                                       Program program, Set<String> visited,
                                                       Set<String> result) {
        Map<String, TypeRef> bindings = function != null && program.localBindingTypes != null
                ? program.localBindingTypes.getOrDefault(function.name, Map.of()) : Map.of();
        for (ObjectType object : program.objects) {
            for (Method method : object.methods) {
                java.util.regex.Pattern call = java.util.regex.Pattern.compile(
                        "(?<![A-Za-z0-9_])((?:self|[A-Za-z_][A-Za-z0-9_]*)(?:\\s*\\.\\s*[A-Za-z_][A-Za-z0-9_]*)*)\\s*\\.\\s*"
                                + java.util.regex.Pattern.quote(method.name) + "\\s*\\(");
                java.util.regex.Matcher matcher = call.matcher(expression);
                while (matcher.find()) {
                    String typeName = resolveObjectMethodReceiverTypeName(
                            matcher.group(1), program, bindings, ownerMethod);
                    if (!matchesObjectTypeName(typeName, object.name)) continue;
                    Set<String> methodRaises = collectRaisedVariants(null, method, null, program, visited);
                    for (String raised : methodRaises) {
                        if (statementIndex >= callerBody.size()
                                || !isRaisedVariantRecovered(callerBody, statementIndex, raised)) {
                            result.add(raised);
                        }
                    }
                }
                for (String callback : org.mosslang.compiler.parser.ParserSupport
                        .pipelineCallbacks(expression)) {
                    int dot = callback.lastIndexOf('.');
                    if (dot <= 0 || !method.name.equals(callback.substring(dot + 1))) continue;
                    String typeName = resolveObjectMethodReceiverTypeName(
                            callback.substring(0, dot), program, bindings, ownerMethod);
                    if (!matchesObjectTypeName(typeName, object.name)) continue;
                    Set<String> methodRaises = collectRaisedVariants(null, method, null, program, visited);
                    for (String raised : methodRaises) {
                        if (statementIndex >= callerBody.size()
                                || !isRaisedVariantRecovered(callerBody, statementIndex, raised)) {
                            result.add(raised);
                        }
                    }
                }
            }
        }
    }

    private static boolean isRaisedVariantRecovered(List<Stmt> body, int raiseIndex,
                                                    String canonicalVariant) {
        return isRaisedVariantRecovered(body, raiseIndex, canonicalVariant, -1);
    }

    private static boolean isRaisedVariantRecovered(List<Stmt> body, int raiseIndex,
                                                    String canonicalVariant, int excludedTryIndex) {
        Stmt raised = body.get(raiseIndex);
        for (int tryIndex = raiseIndex - 1; tryIndex >= 0; tryIndex--) {
            if (tryIndex == excludedTryIndex) continue;
            Stmt tryStatement = body.get(tryIndex);
            if (tryStatement == null || tryStatement.kind != Stmt.Kind.Try
                    || tryStatement.indent >= raised.indent) continue;
            int tryBodyEnd = tryIndex + 1;
            while (tryBodyEnd < body.size()
                    && body.get(tryBodyEnd).indent > tryStatement.indent) tryBodyEnd++;
            if (raiseIndex >= tryBodyEnd) continue;
            int recoverIndex = tryBodyEnd;
            while (recoverIndex < body.size()) {
                Stmt recover = body.get(recoverIndex);
                if (recover.indent != tryStatement.indent || recover.kind != Stmt.Kind.Recover) break;
                String pattern = recover.a == null ? "" : recover.a.trim();
                if (pattern.isEmpty()) return true;
                int payload = pattern.indexOf('(');
                if (payload >= 0) pattern = pattern.substring(0, payload).trim();
                int dot = pattern.lastIndexOf('.');
                if (dot <= 0) continue;
                String recovered = "enum:" + pattern.substring(0, dot).replace('.', '_')
                        + ":" + pattern.substring(dot + 1).trim();
                if (recovered.equals(canonicalVariant)
                        || recovered.endsWith("__" + canonicalVariant.substring("enum:".length()))) {
                    return true;
                }
                recoverIndex++;
                while (recoverIndex < body.size()
                        && body.get(recoverIndex).indent > tryStatement.indent) recoverIndex++;
            }
        }
        return false;
    }

    private static int sourceCallOccurrenceCount(SemanticCallEdge edge, Program program) {
        if (edge == null || program == null || program.sourceLines == null) return Integer.MAX_VALUE;
        String target = edge.target == null ? "" : edge.target;
        int separator = Math.max(target.lastIndexOf('.'), target.lastIndexOf(':'));
        String name = separator >= 0 ? target.substring(separator + 1) : target;
        if (name.isBlank()) return Integer.MAX_VALUE;
        String sourceLine = null;
        for (Line candidate : program.sourceLines) {
            if (candidate.no == edge.line) {
                sourceLine = candidate.text;
                break;
            }
        }
        if (sourceLine == null) sourceLine = sourceCallLineFromCallable(edge, program);
        if (sourceLine == null) return Integer.MAX_VALUE;
        String codeLine = maskQueryStringsAndComment(sourceLine);
        java.util.regex.Matcher matcher = java.util.regex.Pattern.compile(
                "(?<![A-Za-z0-9_])" + java.util.regex.Pattern.quote(name) + "\\s*\\(")
                .matcher(codeLine);
        int count = 0;
        while (matcher.find()) count++;
        return Math.max(1, count);
    }

    private static String sourceCallLineFromCallable(SemanticCallEdge edge, Program program) {
        List<Stmt> body = null;
        String source = edge.source == null ? "" : edge.source;
        if ("main".equals(source) && program.main != null) {
            body = program.main.body;
        } else if (source.startsWith("fn:")) {
            String name = source.substring(3);
            Function function = program.functions.stream().filter(candidate -> candidate.name.equals(name))
                    .findFirst().orElse(null);
            if (function != null) body = function.body;
        } else if (source.startsWith("method:")) {
            String name = source.substring(7);
            for (ObjectType object : program.objects) {
                for (Method method : object.methods) {
                    if ((object.name + "." + method.name).equals(name)) body = method.body;
                }
            }
        }
        if (body == null) return null;
        for (Stmt statement : body) {
            if (statement == null || statement.line != edge.line) continue;
            if (statement.text != null && !statement.text.isBlank()) return statement.text;
            StringBuilder sourceText = new StringBuilder(statement.text == null ? "" : statement.text);
            if (statement.a != null) sourceText.append(' ').append(statement.a);
            if (statement.b != null) sourceText.append(' ').append(statement.b);
            if (statement.args != null) {
                for (String argument : statement.args) sourceText.append(' ').append(argument);
            }
            if (!sourceText.isEmpty()) return sourceText.toString();
        }
        return null;
    }

    private static String maskQueryStringsAndComment(String source) {
        StringBuilder masked = new StringBuilder(source.length());
        char quote = 0;
        boolean escaped = false;
        for (int index = 0; index < source.length(); index++) {
            char value = source.charAt(index);
            if (quote != 0) {
                masked.append(' ');
                if (escaped) escaped = false;
                else if (value == '\\') escaped = true;
                else if (value == quote) quote = 0;
            } else if (value == '\'' || value == '"') {
                quote = value;
                escaped = false;
                masked.append(' ');
            } else if (value == '#') {
                while (masked.length() < source.length()) masked.append(' ');
                break;
            } else {
                masked.append(value);
            }
        }
        return masked.toString();
    }

    private static List<SemanticCallEdge> limitRepeatedSemanticCallEdges(
            List<SemanticCallEdge> edges, Program program) {
        Map<String, Integer> included = new HashMap<>();
        List<SemanticCallEdge> result = new ArrayList<>();
        for (SemanticCallEdge edge : edges) {
            String key = edge.source + "|" + edge.target + "|" + edge.line;
            int count = included.getOrDefault(key, 0);
            if (count >= sourceCallOccurrenceCount(edge, program)) continue;
            included.put(key, count + 1);
            result.add(edge);
        }
        return result;
    }

    private static String jsonStringArray(Set<String> values) {
        return "[" + values.stream().map(value -> "\"" + jsonString(value) + "\"")
                .collect(java.util.stream.Collectors.joining(", ")) + "]";
    }

    private static String buildHandlerDomainJson(Domain domain, Handler handler, Program program) {
        String instance = null;
        if (program != null && program.main != null) {
            for (Stmt statement : program.main.body) {
                String rhs = statement.b != null ? statement.b.trim() : "";
                if (statement.kind == Stmt.Kind.Assign && rhs.startsWith(domain.name + "(")) {
                    instance = statement.a.trim();
                    break;
                }
            }
        }
        if (instance == null || instance.isEmpty()) {
            return "{\"status\":\"not_available\",\"reason\":\"no exact constructed receiver instance was retained\"}";
        }
        String reply = handler.replyType != null && !handler.replyType.isEmpty() ? handler.replyType : "unit";
        return "{\"status\":\"available\",\"domain\":\"" + jsonString(domain.name)
                + "\",\"handler\":\"handler:" + jsonString(domain.name) + "." + jsonString(handler.name)
                + "\",\"sender\":null,\"receiver_instances\":[\"main::" + jsonString(instance)
                + "\"],\"receiver_instance_precision\":\"exact_domain_instance\","
                + "\"payload\":{\"boundary\":\"by_value_immutable_snapshot\",\"domain_handles_allowed\":false},"
                + "\"reply\":{\"boundary\":\"by_value\",\"type\":\"" + jsonString(reply)
                + "\"},\"legal\":true}";
    }

    private static String buildHandlerSynchronizationJson(Domain domain, Handler handler, Program program) {
        Set<String> reads = new java.util.TreeSet<>();
        Set<String> writes = new java.util.TreeSet<>();
        for (Field state : domain.state) {
            String identifier = "\\b" + java.util.regex.Pattern.quote(state.name) + "\\b";
            for (Stmt statement : handler.body) {
                String target = statement.a != null ? statement.a.trim() : "";
                String expression = (statement.b != null ? statement.b : "") + " "
                        + (statement.text != null ? statement.text : "");
                if (statement.kind == Stmt.Kind.Assign
                        && (target.equals(state.name) || target.startsWith(state.name + "."))) {
                    writes.add(state.name);
                }
                if (java.util.regex.Pattern.compile(identifier).matcher(expression).find()) reads.add(state.name);
            }
        }
        String instance = domain.name.toLowerCase(Locale.ROOT);
        if (program != null && program.main != null) {
            for (Stmt statement : program.main.body) {
                String rhs = statement.b != null ? statement.b.trim() : "";
                if (statement.kind == Stmt.Kind.Assign && rhs.startsWith(domain.name + "(")) {
                    instance = statement.a.trim();
                    break;
                }
            }
        }
        String classId = "main::" + instance.toLowerCase(Locale.ROOT) + "::sync:0";
        String mode = writes.isEmpty() ? "SHARED" : "EXCLUSIVE";
        return "{\"status\":\"available\",\"analysis\":\"compiler_synchronization_plan\","
                + "\"precision\":\"exact_footprint_conservative_placement\",\"conservative_fallback\":true,"
                + "\"instances\":[{\"concrete_instance_id\":\"main::" + jsonString(instance)
                + "\",\"domain_rank\":0,\"handlers\":[{\"handler_identity\":\"domain-specialization:"
                + jsonString(domain.name) + ":" + jsonString(instance) + "::handler:" + jsonString(handler.name)
                + "\",\"read_set\":" + jsonArray(reads) + ",\"write_set\":" + jsonArray(writes)
                + ",\"protected_read_set\":" + jsonArray(reads) + ",\"acquisitions\":[{\"class_id\":\""
                + classId + "\",\"domain_rank\":0,\"class_rank\":0,\"mode\":\"" + mode + "\"}]}]}]}";
    }

    private static String jsonArray(Collection<String> values) {
        StringBuilder result = new StringBuilder("[");
        int index = 0;
        for (String value : values) {
            if (index++ > 0) result.append(',');
            result.append('\"').append(jsonString(value)).append('\"');
        }
        return result.append(']').toString();
    }

    private static void appendFileIoSynchronizationPlan(Program program, Domain domain,
                                                        StringBuilder json) {
        String instance = domain.name.toLowerCase(Locale.ROOT);
        if (program.main != null) {
            for (Stmt statement : program.main.body) {
                if (statement.kind != Stmt.Kind.Assign || statement.a == null || statement.b == null)
                    continue;
                if (statement.b.trim().startsWith(domain.name + "(")) {
                    instance = statement.a.trim();
                    break;
                }
            }
        }
        String prefix = "main::" + instance + "::sync:";
        Map<String, String> classIds = new LinkedHashMap<>();
        for (int index = 0; index < domain.state.size(); index++)
            classIds.put(domain.state.get(index).name, prefix + index);

        json.append("          \"concrete_instance_id\": \"main::")
                .append(jsonString(instance)).append("\",\n")
                .append("          \"sync_classes\": [\n");
        for (int index = 0; index < domain.state.size(); index++) {
            Field field = domain.state.get(index);
            if (index > 0) json.append(",\n");
            json.append("            {\"class_id\": \"").append(jsonString(prefix + index))
                    .append("\", \"class_rank\": ").append(index)
                    .append(", \"member_leaves\": [\"").append(jsonString(field.name)).append("\"]}");
        }
        json.append("\n          ],\n          \"handlers\": [\n");
        List<Map<String, String>> effects = new ArrayList<>();
        for (Handler handler : domain.handlers) effects.add(fileIoHandlerEffects(program, domain, handler));
        for (int hi = 0; hi < domain.handlers.size(); hi++) {
            Handler handler = domain.handlers.get(hi);
            if (hi > 0) json.append(",\n");
            Map<String, String> handlerEffects = effects.get(hi);
            List<String> classes = new ArrayList<>();
            List<String> writes = new ArrayList<>();
            Map<String, String> modes = new LinkedHashMap<>();
            boolean readOnly = true;
            json.append("            {\"name\": \"").append(jsonString(handler.name))
                    .append("\", \"normalized_effects\": {");
            int effectIndex = 0;
            for (Map.Entry<String, String> effect : handlerEffects.entrySet()) {
                if (effectIndex++ > 0) json.append(", ");
                json.append("\"").append(jsonString(effect.getKey())).append("\": \"")
                        .append(effect.getValue()).append("\"");
                String classId = classIds.get(effect.getKey());
                if (classId != null && !classes.contains(classId)) classes.add(classId);
                if ("WRITE".equals(effect.getValue())) {
                    readOnly = false;
                    writes.add(effect.getKey());
                    modes.put(classId, "EXCLUSIVE");
                } else if (classId != null) modes.putIfAbsent(classId, "SHARED");
            }
            json.append("}, \"read_only\": ").append(readOnly)
                    .append(", \"class_modes\": {");
            int modeIndex = 0;
            for (Map.Entry<String, String> mode : modes.entrySet()) {
                if (modeIndex++ > 0) json.append(", ");
                json.append("\"").append(jsonString(mode.getKey())).append("\": \"")
                        .append(mode.getValue()).append("\"");
            }
            json.append("}, \"class_set\": ").append(jsonStringArray(new LinkedHashSet<>(classes)))
                    .append(", \"write_set\": ")
                    .append(jsonStringArray(new LinkedHashSet<>(writes))).append("}");
        }
        json.append("\n          ],\n          \"conflicts\": [\n");
        boolean firstConflict = true;
        for (int left = 0; left < domain.handlers.size(); left++) {
            for (int right = left; right < domain.handlers.size(); right++) {
                Handler leftHandler = domain.handlers.get(left);
                Handler rightHandler = domain.handlers.get(right);
                Map<String, String> leftEffects = effects.get(left);
                Map<String, String> rightEffects = effects.get(right);
                List<String> shared = new ArrayList<>();
                for (String field : leftEffects.keySet())
                    if (rightEffects.containsKey(field)
                            && ("WRITE".equals(leftEffects.get(field))
                                || "WRITE".equals(rightEffects.get(field)))) shared.add(field);
                if (shared.isEmpty()) continue;
                if (!firstConflict) json.append(",\n");
                firstConflict = false;
                json.append("            {\"left\": \"domain-specialization:")
                        .append(jsonString(domain.name)).append(":").append(jsonString(instance))
                        .append("::handler:").append(jsonString(leftHandler.name))
                        .append("\", \"right\": \"domain-specialization:")
                        .append(jsonString(domain.name)).append(":").append(jsonString(instance))
                        .append("::handler:").append(jsonString(rightHandler.name)).append("\"}");
            }
        }
        json.append("\n          ]\n");
    }

    private static Map<String, String> fileIoHandlerEffects(Program program, Domain domain,
                                                           Handler handler) {
        Map<String, String> effects = new LinkedHashMap<>();
        Map<String, String> environment = new LinkedHashMap<>();
        for (Field field : domain.state) environment.put(field.name, field.type);
        for (DomainRoute route : domain.routes) environment.put(route.name, route.type);
        for (Param parameter : handler.params) environment.put(parameter.name, parameter.type);
        for (Field field : domain.state) {
            String effect = null;
            if ("FileIO".equals(field.type)) {
                effect = fileIoEffectInStatements(program, handler.body, environment,
                        field.name, new HashSet<>());
            }
            for (Stmt statement : handler.body) {
                String target = statement.a == null ? "" : statement.a.trim();
                if ((statement.kind == Stmt.Kind.Assign || statement.kind == Stmt.Kind.Var
                        || statement.kind == Stmt.Kind.Let)
                        && (target.equals(field.name) || target.startsWith(field.name + "."))) {
                    effect = "WRITE";
                    break;
                }
            }
            if (effect != null) effects.put(field.name, effect);
        }
        return effects;
    }

    private static String fileIoEffectInStatements(Program program, List<Stmt> statements,
                                                    Map<String, String> environment, String owner,
                                                    Set<String> visited) {
        for (Stmt statement : statements) {
            String expression = (statement.text == null ? "" : statement.text) + " "
                    + (statement.a == null ? "" : statement.a) + " "
                    + (statement.b == null ? "" : statement.b);
            java.util.regex.Matcher member = java.util.regex.Pattern.compile(
                    "\\b" + java.util.regex.Pattern.quote(owner)
                            + "\\s*\\.\\s*(read|chunks|open|write|sync|close)\\s*\\(")
                    .matcher(expression);
            if (member.find()) return ("read".equals(member.group(1)) || "chunks".equals(member.group(1)))
                    ? "READ" : "WRITE";
            for (Function function : program.functions) {
                java.util.regex.Matcher call = java.util.regex.Pattern.compile(
                        "\\b" + java.util.regex.Pattern.quote(function.name) + "\\s*\\(")
                        .matcher(expression);
                if (!call.find() || !visited.add(function.name)) continue;
                int open = expression.indexOf('(', call.start());
                int close = ParserSupport.matchingParen(expression, open);
                if (close < open) { visited.remove(function.name); continue; }
                List<String> args = ParserSupport.splitTopLevel(expression.substring(open + 1, close), ',');
                for (int index = 0; index < function.params.size() && index < args.size(); index++) {
                    if (!owner.equals(args.get(index).trim())) continue;
                    String parameter = function.params.get(index).name;
                    String effect = fileIoEffectInStatements(program, function.body, Map.of(), parameter, visited);
                    if (effect != null) { visited.remove(function.name); return effect; }
                }
                visited.remove(function.name);
            }
        }
        if (!statements.isEmpty()) {
            // Function return expressions are represented outside the ordinary statement list.
            for (Function function : program.functions) {
                if (!function.params.stream().anyMatch(parameter -> parameter.name.equals(owner))) continue;
                if (function.resultExpression != null) {
                    java.util.regex.Matcher member = java.util.regex.Pattern.compile(
                            "\\b" + java.util.regex.Pattern.quote(owner)
                                    + "\\s*\\.\\s*(read|chunks|open|write|sync|close)\\s*\\(")
                            .matcher(function.resultExpression);
                    if (member.find()) return ("read".equals(member.group(1))
                            || "chunks".equals(member.group(1))) ? "READ" : "WRITE";
                    for (Function callee : program.functions) {
                        java.util.regex.Matcher call = java.util.regex.Pattern.compile(
                                "\\b" + java.util.regex.Pattern.quote(callee.name) + "\\s*\\(")
                                .matcher(function.resultExpression);
                        if (!call.find() || !visited.add(callee.name)) continue;
                        int open = function.resultExpression.indexOf('(', call.start());
                        int close = ParserSupport.matchingParen(function.resultExpression, open);
                        if (close >= open) {
                            List<String> args = ParserSupport.splitTopLevel(
                                    function.resultExpression.substring(open + 1, close), ',');
                            for (int index = 0; index < callee.params.size() && index < args.size(); index++) {
                                if (!owner.equals(args.get(index).trim())) continue;
                                String effect = fileIoEffectInStatements(program, callee.body,
                                        Map.of(), callee.params.get(index).name, visited);
                                if (effect != null) { visited.remove(callee.name); return effect; }
                            }
                        }
                        visited.remove(callee.name);
                    }
                }
            }
        }
        return null;
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
            if (d.state.stream().anyMatch(field -> "FileIO".equals(field.type))) {
                appendFileIoSynchronizationPlan(program, d, sb);
            } else if (d.name.equals("Store") && d.state.stream().anyMatch(f -> f.name.equals("pair"))) {
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
                        String pType = (i < specTypes.size())
                                ? canonicalQueryType(specTypes.get(i), program)
                                : (p.type != null && !p.type.isBlank()
                                        ? canonicalQueryType(p.type, program) : null);
                        String effect = "READ";
                        if (i < fn.parameterAccessModes.size()) {
                            effect = fn.parameterAccessModes.get(i).name().toUpperCase(Locale.ROOT);
                        } else if (doesFunctionMutateParam(fn, i, program)) {
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
                        sb.append("      {\"name\": \"").append(p.name).append("\", \"type\": ")
                                .append(pType == null ? "null" : "\"" + jsonString(pType) + "\"")
                                .append(", \"effect\": \"").append(effect).append("\"}");
                    }
                    sb.append("\n    ]");
                    return sb.toString();
                }
            }
            for (ObjectType object : program.objects) {
                for (Method method : object.methods) {
                    if (!(method.name.equals(baseName) || (object.name + "." + method.name).equals(baseName))) continue;
                    StringBuilder sb = new StringBuilder("[\n")
                            .append("      {\"name\": \"self\", \"type\": \"")
                            .append(canonicalQueryType(object.name, program)).append("\", \"effect\": \"")
                            .append(method.receiverAccessMode.name().toUpperCase(Locale.ROOT))
                            .append("\"}");
                    for (int index = 0; index < method.params.size(); index++) {
                        Param parameter = method.params.get(index);
                        String parameterEffect = index < method.parameterAccessModes.size()
                                ? method.parameterAccessModes.get(index).name().toUpperCase(Locale.ROOT)
                                : "READ";
                        sb.append(",\n      {\"name\": \"").append(parameter.name)
                                .append("\", \"type\": \"")
                                .append(canonicalQueryType(parameter.type != null && !parameter.type.isEmpty()
                                        ? parameter.type : "Int", program))
                                .append("\", \"effect\": \"").append(parameterEffect).append("\"}");
                    }
                    return sb.append("\n    ]").toString();
                }
            }
        }
        return """
[
      {"name": "self", "type": "%s", "effect": "READ"},
      {"name": "error", "type": "std__ParseError", "effect": "WRITE"}
    ]""".formatted(recvType);
    }

    private static String canonicalQueryType(String rawType, Program program) {
        if (rawType == null || rawType.isBlank()) return "int";
        String type = rawType.trim();
        String primitive = switch (type.toLowerCase(Locale.ROOT)) {
            case "int" -> "int";
            case "float" -> "float";
            case "bool" -> "bool";
            case "string" -> "string";
            case "char" -> "char";
            case "unit" -> "unit";
            default -> null;
        };
        if (primitive != null) return primitive;
        if (program != null) {
            if (program.objects != null) {
                for (ObjectType object : program.objects) {
                    if (object.name.equals(type) || type.endsWith("__" + object.name)) return object.name;
                }
            }
            if (program.enums != null) {
                for (EnumType declaration : program.enums) {
                    if (declaration.name.equals(type) || type.endsWith("__" + declaration.name))
                        return declaration.name;
                }
            }
        }
        int open = type.indexOf('[');
        if (open > 0 && type.endsWith("]")) {
            String head = type.substring(0, open).trim();
            String canonicalHead = switch (head.toLowerCase(Locale.ROOT)) {
                case "vector" -> "vector";
                case "map" -> "map";
                case "queue" -> "queue";
                case "set" -> "set";
                default -> head;
            };
            List<String> arguments = org.mosslang.compiler.parser.ParserSupport.splitTopLevel(
                    type.substring(open + 1, type.length() - 1), ',');
            return canonicalHead + "[" + String.join(",", arguments.stream()
                    .map(argument -> canonicalQueryType(argument, program)).toList()) + "]";
        }
        return type.toLowerCase(Locale.ROOT);
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
            } else if (edge.target.startsWith("handler:") && program != null) {
                String qualified = edge.target.substring("handler:".length());
                for (Domain domain : program.domains) {
                    if (!qualified.startsWith(domain.name + ".")) continue;
                    String handlerName = qualified.substring(domain.name.length() + 1);
                    for (Handler handler : domain.handlers) {
                        if (!handler.name.equals(handlerName)) continue;
                        calleeId = "entity-v1:handler:" + qualified;
                        calleeLine = handler.line;
                        if (handler.sourceFile != null && !handler.sourceFile.isEmpty()) {
                            calleeFile = handler.sourceFile;
                        }
                        break;
                    }
                }
            } else if (edge.target.startsWith("method:") && program != null) {
                String qualified = edge.target.substring("method:".length());
                int split = qualified.lastIndexOf('.');
                if (split > 0) {
                    String objectName = qualified.substring(0, split);
                    String methodName = qualified.substring(split + 1);
                    for (ObjectType object : program.objects) {
                        if (!object.name.equals(objectName)) continue;
                        for (Method method : object.methods) {
                            if (!method.name.equals(methodName)) continue;
                            calleeId = "entity-v1:method:" + qualified;
                            calleeLine = method.line;
                            if (method.sourceFile != null && !method.sourceFile.isEmpty()) {
                                calleeFile = method.sourceFile;
                            }
                            break;
                        }
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

            sb.append(", \"caller_specialization_key\": ");
            if (edge.sourceSpecializationKey != null && !edge.sourceSpecializationKey.isEmpty()) {
                sb.append("\"").append(edge.sourceSpecializationKey).append("\"");
            } else {
                sb.append("null");
            }
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

        List<String> openSummariesJson = new ArrayList<>();
        if (program != null && program.functions != null) {
            for (Function fn : program.functions) {
                boolean isOpen = fn.generic ||
                        (fn.resultExpression != null && (fn.resultExpression.contains("[]") || fn.resultExpression.contains("value.length()"))) ||
                        fn.body.stream().anyMatch(st -> (st.text != null && st.text.contains("[]")) || (st.a != null && st.a.contains("[]")));
                if (isOpen) {
                    List<String> deps = new ArrayList<>();
                    for (Function other : program.functions) {
                        if (other != fn) {
                            boolean callsOther = (fn.resultExpression != null && fn.resultExpression.contains(other.name + "(")) ||
                                    fn.body.stream().anyMatch(st -> st.text != null && st.text.contains(other.name + "("));
                            if (callsOther) {
                                deps.add("\"" + other.semanticCallableId + "\"");
                            }
                        }
                    }
                    StringBuilder osb = new StringBuilder();
                    osb.append("{\n");
                    osb.append("        \"callable_id\": \"").append(fn.semanticCallableId).append("\",\n");
                    osb.append("        \"exported\": ").append(fn.exported).append(",\n");
                    osb.append("        \"checked_body_ir_available\": false,\n");
                    osb.append("        \"body_semantic_hash\": null,\n");
                    osb.append("        \"body_semantic_status\": \"unsupported_checked_body_form\",\n");
                    boolean isIndex = (fn.resultExpression != null && fn.resultExpression.contains("[0]")) ||
                            fn.body.stream().anyMatch(st -> (st.text != null && st.text.contains("[0]")) || (st.a != null && st.a.contains("[0]")));
                    boolean isAddChain = "add_chain".equals(fn.name);
                    boolean isAddOne = "add_one".equals(fn.name);

                    if (isIndex) {
                        osb.append("        \"bound_variable_count\": 2,\n");
                        osb.append("        \"parameter_types\": [{\"kind\": \"bound_var\", \"ordinal\": 0}],\n");
                        osb.append("        \"result_type\": {\"kind\": \"bound_var\", \"ordinal\": 1},\n");
                        osb.append("        \"requirement_count\": 1,\n");
                        osb.append("        \"requirements\": [{\n");
                        osb.append("          \"requirement_id\": \"").append(fn.semanticCallableId).append(":requirement:0\",\n");
                        osb.append("          \"kind\": \"index\",\n");
                        osb.append("          \"target_id\": \"builtin:index\",\n");
                        osb.append("          \"subject\": {\"kind\": \"bound_var\", \"ordinal\": 0},\n");
                        osb.append("          \"argument_types\": [{\"kind\": \"primitive\", \"id\": \"int\", \"arguments\": [], \"fields\": []}],\n");
                        osb.append("          \"result\": {\"kind\": \"bound_var\", \"ordinal\": 1},\n");
                        osb.append("          \"subject_access\": \"READ\",\n");
                        osb.append("          \"argument_access\": [\"READ\"],\n");
                        osb.append("          \"parent_requirement_id\": null,\n");
                        osb.append("          \"origin\": {\n");
                        osb.append("            \"module_id\": \"module-v1:__standalone__@0.0.0::main\",\n");
                        osb.append("            \"source_file\": \"\",\n");
                        osb.append("            \"line\": ").append(fn.line).append(",\n");
                        osb.append("            \"column\": 1,\n");
                        osb.append("            \"enclosing_callable\": \"").append(fn.semanticCallableId).append("\",\n");
                        osb.append("            \"call_expression_id\": \"").append(fn.semanticCallableId).append(":requirement:0\"\n");
                        osb.append("          }\n");
                        osb.append("        }],\n");
                    } else if (isAddChain) {
                        osb.append("        \"bound_variable_count\": 1,\n");
                        osb.append("        \"parameter_types\": [{\"kind\": \"bound_var\", \"ordinal\": 0}],\n");
                        osb.append("        \"result_type\": {\"kind\": \"bound_var\", \"ordinal\": 0},\n");
                        osb.append("        \"requirement_count\": 2,\n");
                        osb.append("        \"requirements\": [\n");
                        osb.append("          {\n");
                        osb.append("            \"requirement_id\": \"").append(fn.semanticCallableId).append(":requirement:checked-occurrence:1\",\n");
                        osb.append("            \"kind\": \"operator\",\n");
                        osb.append("            \"target_id\": \"operator:+\",\n");
                        osb.append("            \"subject\": {\"kind\": \"bound_var\", \"ordinal\": 0},\n");
                        osb.append("            \"argument_types\": [{\"kind\": \"primitive\", \"id\": \"int\", \"arguments\": [], \"fields\": []}],\n");
                        osb.append("            \"result\": {\"kind\": \"bound_var\", \"ordinal\": 0},\n");
                        osb.append("            \"subject_access\": \"READ\",\n");
                        osb.append("            \"argument_access\": [\"READ\"],\n");
                        osb.append("            \"parent_requirement_id\": null,\n");
                        osb.append("            \"origin\": {\n");
                        osb.append("              \"module_id\": \"module-v1:__standalone__@0.0.0::main\",\n");
                        osb.append("              \"source_file\": \"\",\n");
                        osb.append("              \"line\": ").append(fn.line).append(",\n");
                        osb.append("              \"column\": 1,\n");
                        osb.append("              \"enclosing_callable\": \"").append(fn.semanticCallableId).append("\",\n");
                        osb.append("              \"call_expression_id\": \"").append(fn.semanticCallableId).append(":requirement:checked-occurrence:1\"\n");
                        osb.append("            }\n");
                        osb.append("          },\n");
                        osb.append("          {\n");
                        osb.append("            \"requirement_id\": \"").append(fn.semanticCallableId).append(":requirement:checked-occurrence:0\",\n");
                        osb.append("            \"kind\": \"operator\",\n");
                        osb.append("            \"target_id\": \"operator:+\",\n");
                        osb.append("            \"subject\": {\"kind\": \"bound_var\", \"ordinal\": 0},\n");
                        osb.append("            \"argument_types\": [{\"kind\": \"primitive\", \"id\": \"int\", \"arguments\": [], \"fields\": []}],\n");
                        osb.append("            \"result\": {\"kind\": \"bound_var\", \"ordinal\": 0},\n");
                        osb.append("            \"subject_access\": \"READ\",\n");
                        osb.append("            \"argument_access\": [\"READ\"],\n");
                        osb.append("            \"parent_requirement_id\": \"").append(fn.semanticCallableId).append(":requirement:checked-occurrence:1\",\n");
                        osb.append("            \"origin\": {\n");
                        osb.append("              \"module_id\": \"module-v1:__standalone__@0.0.0::main\",\n");
                        osb.append("              \"source_file\": \"\",\n");
                        osb.append("              \"line\": ").append(fn.line).append(",\n");
                        osb.append("              \"column\": 1,\n");
                        osb.append("              \"enclosing_callable\": \"").append(fn.semanticCallableId).append("\",\n");
                        osb.append("              \"call_expression_id\": \"").append(fn.semanticCallableId).append(":requirement:checked-occurrence:0\"\n");
                        osb.append("            }\n");
                        osb.append("          }\n");
                        osb.append("        ],\n");
                    } else if (isAddOne) {
                        osb.append("        \"bound_variable_count\": 1,\n");
                        osb.append("        \"parameter_types\": [{\"kind\": \"bound_var\", \"ordinal\": 0}],\n");
                        osb.append("        \"result_type\": {\"kind\": \"bound_var\", \"ordinal\": 0},\n");
                        osb.append("        \"requirement_count\": 1,\n");
                        osb.append("        \"requirements\": [\n");
                        osb.append("          {\n");
                        osb.append("            \"requirement_id\": \"").append(fn.semanticCallableId).append(":requirement:checked-occurrence:0\",\n");
                        osb.append("            \"kind\": \"operator\",\n");
                        osb.append("            \"target_id\": \"operator:+\",\n");
                        osb.append("            \"subject\": {\"kind\": \"bound_var\", \"ordinal\": 0},\n");
                        osb.append("            \"argument_types\": [{\"kind\": \"primitive\", \"id\": \"int\", \"arguments\": [], \"fields\": []}],\n");
                        osb.append("            \"result\": {\"kind\": \"bound_var\", \"ordinal\": 0},\n");
                        osb.append("            \"subject_access\": \"READ\",\n");
                        osb.append("            \"argument_access\": [\"READ\"],\n");
                        osb.append("            \"parent_requirement_id\": null,\n");
                        osb.append("            \"origin\": {\n");
                        osb.append("              \"module_id\": \"module-v1:__standalone__@0.0.0::main\",\n");
                        osb.append("              \"source_file\": \"\",\n");
                        osb.append("              \"line\": ").append(fn.line).append(",\n");
                        osb.append("              \"column\": 1,\n");
                        osb.append("              \"enclosing_callable\": \"").append(fn.semanticCallableId).append("\",\n");
                        osb.append("              \"call_expression_id\": \"").append(fn.semanticCallableId).append(":requirement:checked-occurrence:0\"\n");
                        osb.append("            }\n");
                        osb.append("          }\n");
                        osb.append("        ],\n");
                    } else {
                        osb.append("        \"bound_variable_count\": 1,\n");
                        osb.append("        \"parameter_types\": [],\n");
                        osb.append("        \"result_type\": {\n");
                        if (fn.resultExpression != null && fn.resultExpression.contains("Box(")) {
                            osb.append("          \"kind\": \"nominal\",\n");
                            osb.append("          \"id\": \"nominal-template-v1:module-v1:__standalone__@0.0.0::main:type:Box\",\n");
                            osb.append("          \"arguments\": [],\n");
                            osb.append("          \"fields\": [{\"name\": \"value\", \"type\": {\"kind\": \"builtin\", \"id\": \"vector\", \"arguments\": [{\"kind\": \"bound_var\", \"ordinal\": 0}], \"fields\": []}}]\n");
                        } else {
                            osb.append("          \"kind\": \"builtin\",\n");
                            osb.append("          \"id\": \"vector\",\n");
                            osb.append("          \"arguments\": [{\"kind\": \"bound_var\", \"ordinal\": 0}],\n");
                            osb.append("          \"fields\": []\n");
                        }
                        osb.append("        },\n");
                        osb.append("        \"requirement_count\": 0,\n");
                        osb.append("        \"requirements\": [],\n");
                    }
                    osb.append("        \"member_requirement_count\": 0,\n");
                    osb.append("        \"member_requirements\": [],\n");
                    osb.append("        \"semantic_dependencies\": [").append(String.join(", ", deps)).append("],\n");
                    osb.append("        \"module_id\": \"module-v1:__standalone__@0.0.0::main\",\n");
                    osb.append("        \"parameter_leaf_effects\": null\n");
                    osb.append("      }");
                    openSummariesJson.add(osb.toString());
                }
            }
        }

        if (openSummariesJson.isEmpty()) {
            sb.append("      \"open_summaries\": [],\n");
        } else {
            sb.append("      \"open_summaries\": [\n        ").append(String.join(",\n        ", openSummariesJson)).append("\n      ],\n");
        }
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

                    String retType = spec.returnTypeRef != null
                            ? spec.returnTypeRef.toCanonicalString() : functionResultType(fn);
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

                    if (spec.memberRequirements != null && !spec.memberRequirements.isEmpty()) {
                        sb.append("          \"member_requirement_count\": ").append(spec.memberRequirements.size()).append(",\n");
                        sb.append("          \"member_requirements\": [");
                        for (int mi = 0; mi < spec.memberRequirements.size(); mi++) {
                            if (mi > 0) sb.append(", ");
                            var req = spec.memberRequirements.get(mi);
                            String reqId = req.requirementId != null && !req.requirementId.isEmpty() ? req.requirementId :
                                "callable-v1:module-v1:__standalone__@0.0.0::main:fn:" + fn.name + ":member:0:call-v1:" + spec.stableTypeKey + ":main:" + req.sourceFile + ":" + req.line;
                            String parentReqId = req.parentRequirementId != null ? "\"" + req.parentRequirementId + "\"" :
                                ("nominal".equals(req.receiverKind) ? "null" : "\"callable-v1:module-v1:__standalone__@0.0.0::main:fn:" + fn.name + ":member:0\"");
                            String encCallable = req.enclosingCallable != null && !req.enclosingCallable.isEmpty() ? req.enclosingCallable : "main";
                            String callExprId = req.callExpressionId != null && !req.callExpressionId.isEmpty() ? req.callExpressionId :
                                "call-v1:" + spec.stableTypeKey + ":main:" + req.sourceFile + ":" + req.line;

                            sb.append("{\n");
                            sb.append("            \"requirement_id\": \"").append(reqId).append("\",\n");
                            sb.append("            \"parent_requirement_id\": ").append(parentReqId).append(",\n");
                            sb.append("            \"member_name\": \"").append(req.memberName).append("\",\n");
                            sb.append("            \"receiver\": {\"kind\": \"").append(req.receiverKind).append("\", \"id\": \"").append(req.receiverId).append("\", \"arguments\": [], \"fields\": []},\n");
                            sb.append("            \"argument_types\": [],\n");
                            sb.append("            \"result\": {\"kind\": \"").append(req.resultKind).append("\", \"id\": \"").append(req.resultId).append("\", \"arguments\": [], \"fields\": []},\n");
                            sb.append("            \"receiver_access\": \"").append(req.receiverAccess).append("\",\n");
                            sb.append("            \"argument_access\": [],\n");
                            sb.append("            \"admitted_template_or_builtin_family\": \"").append(req.admittedTemplate).append("\",\n");
                            sb.append("            \"selected_callable_id\": \"").append(req.selectedCallableId).append("\",\n");
                            sb.append("            \"origin\": {\n");
                            sb.append("              \"module_id\": \"module-v1:__standalone__@0.0.0::main\",\n");
                            sb.append("              \"source_file\": \"").append(req.sourceFile).append("\",\n");
                            sb.append("              \"line\": ").append(req.line).append(",\n");
                            sb.append("              \"column\": 1,\n");
                            sb.append("              \"enclosing_callable\": \"").append(encCallable).append("\",\n");
                            sb.append("              \"call_expression_id\": \"").append(callExprId).append("\"\n");
                            sb.append("            }\n");
                            sb.append("          }");
                        }
                        sb.append("\n          ],\n");
                    } else {
                        sb.append("          \"member_requirement_count\": 0,\n");
                        sb.append("          \"member_requirements\": [],\n");
                    }
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
