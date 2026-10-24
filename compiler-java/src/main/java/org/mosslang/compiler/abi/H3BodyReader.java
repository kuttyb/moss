package org.mosslang.compiler.abi;

import org.mosslang.compiler.types.*;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

public class H3BodyReader {
    private final String bytes;
    private final TypeContext context;
    private int position = 0;

    public H3BodyReader(String bytes) {
        this(bytes, null);
    }

    public H3BodyReader(String bytes, TypeContext context) {
        this.bytes = bytes != null ? bytes : "";
        this.context = context;
    }

    public String field() {
        int begin = position;
        while (position < bytes.length() && Character.isDigit(bytes.charAt(position))) {
            position++;
        }
        if (begin == position || position >= bytes.length() || bytes.charAt(position) != ':' ||
            (position - begin > 1 && bytes.charAt(begin) == '0')) {
            throw new IllegalArgumentException("malformed checked body field length at position " + position);
        }
        int length = Integer.parseInt(bytes.substring(begin, position));
        position++; // Skip ':'
        if (position + length > bytes.length()) {
            throw new IllegalArgumentException("checked body field exceeds bounds");
        }
        String value = bytes.substring(position, position + length);
        position += length;
        return value;
    }

    public int number() {
        String f = field();
        if (f.isEmpty()) {
            throw new IllegalArgumentException("checked body integer is empty");
        }
        return Integer.parseInt(f);
    }

    public TypeRef readType() {
        int kindOrdinal = number();
        if (kindOrdinal < 0 || kindOrdinal >= TypeKind.values().length) {
            throw new IllegalArgumentException("unknown TypeKind ordinal: " + kindOrdinal);
        }
        TypeKind kind = TypeKind.values()[kindOrdinal];
        String identity = field();

        int typeArgCount = number();
        List<TypeRef> typeArgs = new ArrayList<>(typeArgCount);
        for (int i = 0; i < typeArgCount; i++) {
            typeArgs.add(readType());
        }

        int fieldSubCount = number();
        List<TypeRef.FieldSub> fieldSubs = new ArrayList<>(fieldSubCount);
        for (int i = 0; i < fieldSubCount; i++) {
            String name = field();
            TypeRef t = readType();
            fieldSubs.add(new TypeRef.FieldSub(name, t));
        }

        switch (kind) {
            case InferenceVar -> {
                int varOrdinal = Integer.parseInt(identity);
                return TypeRef.var(new TypeVarId(0, 0, varOrdinal));
            }
            case Primitive -> {
                return TypeRef.primitive(identity);
            }
            case Builtin -> {
                return TypeRef.builtin(identity, typeArgs);
            }
            case Unit -> {
                return TypeRef.unit();
            }
            case NominalInstance -> {
                NominalDeclaration decl = context != null ? context.findId(identity) : null;
                if (decl == null) {
                    decl = new NominalDeclaration(DeclarationKind.Record, identity, "module-v1:imported", identity, identity, Collections.emptyList());
                }
                return TypeRef.nominalBound(decl, identity, fieldSubs);
            }
            case Enum -> {
                NominalDeclaration decl = context != null ? context.findId(identity) : null;
                if (decl == null) {
                    decl = new NominalDeclaration(DeclarationKind.Enum, identity, "module-v1:imported", identity, identity, Collections.emptyList());
                }
                return TypeRef.enumeration(decl, identity);
            }
            case Domain -> {
                NominalDeclaration decl = context != null ? context.findId(identity) : null;
                if (decl == null) {
                    decl = new NominalDeclaration(DeclarationKind.Domain, identity, "module-v1:imported", identity, identity, Collections.emptyList());
                }
                return TypeRef.domain(decl, identity);
            }
            default -> {
                return TypeRef.primitive(identity);
            }
        }
    }

    public CheckedExpr readExpr() {
        int tag = number();
        CheckedExprKind kind = null;
        for (CheckedExprKind k : CheckedExprKind.values()) {
            if (k.tag == tag) {
                kind = k;
                break;
            }
        }
        if (kind == null) {
            throw new IllegalArgumentException("Unknown CheckedExprKind tag: " + tag);
        }
        TypeRef type = readType();
        CheckedExpr expr = new CheckedExpr();
        expr.kind = kind;
        expr.type = type;

        switch (kind) {
            case Parameter -> {
                expr.ordinal = number();
            }
            case IntegerLiteral -> {
                expr.literalValue = field();
            }
            case FloatLiteral, StringLiteral -> {
                expr.literalValue = field();
            }
            case BoolLiteral -> {
                String val = field();
                expr.literalValue = "1".equals(val) ? "true" : "false";
            }
            case LocalRead -> {
                expr.ordinal = number();
            }
            case Add -> {
                int arity = number();
                for (int i = 0; i < arity; i++) {
                    expr.operands.add(readExpr());
                }
            }
            case Unary -> {
                int opTag = number();
                for (CheckedUnaryOp op : CheckedUnaryOp.values()) {
                    if (op.tag == opTag) {
                        expr.unaryOp = op;
                        break;
                    }
                }
                expr.operands.add(readExpr());
            }
            case Binary -> {
                int opTag = number();
                for (CheckedBinaryOp op : CheckedBinaryOp.values()) {
                    if (op.tag == opTag) {
                        expr.binaryOp = op;
                        break;
                    }
                }
                expr.operands.add(readExpr());
                expr.operands.add(readExpr());
            }
            case Field -> {
                expr.semanticTargetId = field();
                expr.memberOrdinal = number();
            }
            default -> {
                expr.literalValue = field();
            }
        }
        return expr;
    }

    public CheckedStmt readStmt() {
        int tag = number();
        CheckedStmtKind kind = null;
        for (CheckedStmtKind k : CheckedStmtKind.values()) {
            if (k.tag == tag) {
                kind = k;
                break;
            }
        }
        if (kind == null) {
            throw new IllegalArgumentException("Unknown CheckedStmtKind tag: " + tag);
        }
        int localOrdinal = number();
        int hasExpr = number();
        CheckedExpr expr = (hasExpr == 1) ? readExpr() : null;

        int operandCount = number();
        List<CheckedExpr> operands = new ArrayList<>(operandCount);
        for (int i = 0; i < operandCount; i++) {
            operands.add(readExpr());
        }

        int bodyCount = number();
        List<CheckedStmt> body = new ArrayList<>(bodyCount);
        for (int i = 0; i < bodyCount; i++) {
            body.add(readStmt());
        }

        CheckedStmt stmt = new CheckedStmt();
        stmt.kind = kind;
        stmt.localOrdinal = localOrdinal;
        stmt.expr = expr;
        stmt.type = expr != null ? expr.type : null;
        stmt.operands = operands;
        stmt.body = body;
        return stmt;
    }

    public List<CheckedStmt> read() {
        if (!bytes.startsWith(CheckedBodyCodec.HEADER)) {
            throw new IllegalArgumentException("Invalid checked body header: expected " + CheckedBodyCodec.HEADER);
        }
        position = CheckedBodyCodec.HEADER.length();
        int count = number();
        List<CheckedStmt> statements = new ArrayList<>(count);
        for (int i = 0; i < count; i++) {
            statements.add(readStmt());
        }
        return statements;
    }

    public boolean isDone() {
        return position == bytes.length();
    }
}
