package org.mosslang.compiler.abi;

import org.mosslang.compiler.diagnostics.CompileError;
import org.mosslang.compiler.types.*;

import java.nio.ByteBuffer;
import java.nio.charset.CharacterCodingException;
import java.nio.charset.CodingErrorAction;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

/** Reader for the canonical C++ checked-body-v1 field encoding. */
public class H3BodyReader {
    private static final int MAX_COLLECTION_ITEMS = 100_000;
    private static final int MAX_DEPTH = 128;
    private final byte[] bytes;
    private final TypeContext context;
    private int position;

    public H3BodyReader(String bytes) { this(bytes, null); }

    public H3BodyReader(String bytes, TypeContext context) {
        this.bytes = (bytes != null ? bytes : "").getBytes(StandardCharsets.UTF_8);
        this.context = context;
    }

    public String field() {
        int begin = position;
        while (position < bytes.length && bytes[position] >= '0' && bytes[position] <= '9') position++;
        if (begin == position || position >= bytes.length || bytes[position] != ':'
                || (position - begin > 1 && bytes[begin] == '0')) {
            throw invalid("malformed checked body field length at byte " + position);
        }
        int length;
        try { length = Integer.parseInt(new String(bytes, begin, position - begin, StandardCharsets.US_ASCII)); }
        catch (NumberFormatException error) { throw invalid("checked body field length is malformed"); }
        position++;
        if (length < 0 || length > bytes.length - position) throw invalid("checked body field exceeds bounds");
        String value;
        try {
            value = StandardCharsets.UTF_8.newDecoder()
                    .onMalformedInput(CodingErrorAction.REPORT)
                    .onUnmappableCharacter(CodingErrorAction.REPORT)
                    .decode(ByteBuffer.wrap(bytes, position, length)).toString();
        } catch (CharacterCodingException error) {
            throw invalid("checked body field is not valid UTF-8");
        }
        position += length;
        return value;
    }

    public int number() {
        String value = field();
        try {
            int parsed = Integer.parseInt(value);
            if (!Integer.toString(parsed).equals(value)) throw new NumberFormatException();
            return parsed;
        } catch (NumberFormatException error) { throw invalid("checked body integer is malformed"); }
    }

    private int count(String what) {
        int value = number();
        if (value < 0 || value > MAX_COLLECTION_ITEMS || value > bytes.length) {
            throw invalid("checked body " + what + " exceeds its limit");
        }
        return value;
    }

    private CompileError invalid(String message) {
        return new CompileError(0, message, "SOURCE_FREE_CONTRACT_INVALID");
    }

    public TypeRef readType() {
        int kindOrdinal = number();
        if (kindOrdinal < 0 || kindOrdinal >= TypeKind.values().length) throw invalid("checked body TypeKind is unknown");
        TypeKind kind = TypeKind.values()[kindOrdinal];
        String identity = field();
        int typeArgCount = count("type argument count");
        List<TypeRef> typeArgs = new ArrayList<>(typeArgCount);
        for (int i = 0; i < typeArgCount; i++) typeArgs.add(readType());
        int fieldSubCount = count("field substitution count");
        List<TypeRef.FieldSub> fieldSubs = new ArrayList<>(fieldSubCount);
        for (int i = 0; i < fieldSubCount; i++) fieldSubs.add(new TypeRef.FieldSub(field(), readType()));

        return switch (kind) {
            case InferenceVar -> TypeRef.var(new TypeVarId(0, 0, parseNonNegative(identity, "type variable ordinal")));
            case Primitive -> TypeRef.primitive(identity);
            case Builtin -> TypeRef.builtin(identity, typeArgs);
            case Unit -> TypeRef.unit();
            case NominalInstance -> {
                NominalDeclaration decl = context != null ? context.findId(identity) : null;
                if (decl == null) decl = new NominalDeclaration(DeclarationKind.Record,
                        identity, "module-v1:imported", identity, identity, Collections.emptyList());
                yield TypeRef.nominalBound(decl, identity, fieldSubs);
            }
            case Enum -> {
                NominalDeclaration decl = context != null ? context.findId(identity) : null;
                if (decl == null) decl = new NominalDeclaration(DeclarationKind.Enum,
                        identity, "module-v1:imported", identity, identity, Collections.emptyList());
                yield TypeRef.enumeration(decl, identity);
            }
            case Domain -> {
                NominalDeclaration decl = context != null ? context.findId(identity) : null;
                if (decl == null) decl = new NominalDeclaration(DeclarationKind.Domain,
                        identity, "module-v1:imported", identity, identity, Collections.emptyList());
                yield TypeRef.domain(decl, identity);
            }
            case TraitConstraint -> TypeRef.traitConstraint(identity);
        };
    }

    private int parseNonNegative(String value, String what) {
        try {
            int parsed = Integer.parseInt(value);
            if (parsed < 0 || !Integer.toString(parsed).equals(value)) throw new NumberFormatException();
            return parsed;
        } catch (NumberFormatException error) { throw invalid("checked body " + what + " is malformed"); }
    }

    private CheckedExpr readExpr(int depth) {
        if (depth > MAX_DEPTH) throw invalid("checked body expression nesting exceeds its limit");
        int tag = number();
        CheckedExprKind kind = null;
        for (CheckedExprKind candidate : CheckedExprKind.values()) if (candidate.tag == tag) kind = candidate;
        if (kind == null) throw invalid("checked body expression kind is unknown");
        CheckedExpr expr = new CheckedExpr();
        expr.kind = kind;
        expr.type = readType();
        switch (kind) {
            case Parameter, LocalRead -> expr.ordinal = parseNonNegative(field(), "expression ordinal");
            case IntegerLiteral, FloatLiteral, StringLiteral -> expr.literalValue = field();
            case BoolLiteral -> {
                String value = field();
                if (!value.equals("0") && !value.equals("1")) throw invalid("checked body boolean literal is invalid");
                expr.literalValue = value.equals("1") ? "true" : "false";
            }
            case Add -> {
                int arity = count("addition arity");
                if (arity != 2) throw invalid("checked body addition arity is invalid");
                expr.operands.add(readExpr(depth + 1));
                expr.operands.add(readExpr(depth + 1));
            }
            case Unary -> {
                expr.unaryOp = readUnary(number());
                expr.operands.add(readExpr(depth + 1));
            }
            case Binary -> {
                expr.binaryOp = readBinary(number());
                expr.operands.add(readExpr(depth + 1));
                expr.operands.add(readExpr(depth + 1));
            }
            case Field -> {
                expr.semanticTargetId = field();
                expr.memberOrdinal = parseNonNegative(field(), "member ordinal");
                expr.operands.add(readExpr(depth + 1));
            }
            case Call -> {
                expr.semanticTargetId = field();
                int arity = count("call arity");
                for (int i = 0; i < arity; i++) expr.operands.add(readExpr(depth + 1));
            }
            case VectorLiteral -> {
                int arity = count("vector literal arity");
                for (int i = 0; i < arity; i++) expr.operands.add(readExpr(depth + 1));
            }
            case MapLiteral -> {
                int pairs = count("map literal pair count");
                if (pairs > 32 || pairs > MAX_COLLECTION_ITEMS / 2) throw invalid("checked body map literal exceeds its limit");
                for (int i = 0; i < pairs * 2; i++) expr.operands.add(readExpr(depth + 1));
            }
            case CollectionCount -> {
                expr.semanticTargetId = field();
                expr.operands.add(readExpr(depth + 1));
            }
            case Index -> {
                expr.semanticTargetId = field();
                expr.operands.add(readExpr(depth + 1));
                expr.operands.add(readExpr(depth + 1));
            }
            case CollectionPush -> {
                expr.semanticTargetId = field();
                int arity = count("collection push arity");
                if (arity != 2) throw invalid("checked body collection push arity is invalid");
                expr.operands.add(readExpr(depth + 1));
                expr.operands.add(readExpr(depth + 1));
            }
            default -> throw invalid("Java checked-body reader does not support expression kind " + kind);
        }
        return expr;
    }

    private CheckedUnaryOp readUnary(int tag) {
        for (CheckedUnaryOp op : CheckedUnaryOp.values()) if (op.tag == tag) return op;
        throw invalid("checked body unary operator is unknown");
    }

    private CheckedBinaryOp readBinary(int tag) {
        for (CheckedBinaryOp op : CheckedBinaryOp.values()) if (op.tag == tag) return op;
        throw invalid("checked body binary operator is unknown");
    }

    public CheckedExpr readExpr() { return readExpr(0); }

    private CheckedStmt readStmt(int depth) {
        return readStmt(number(), depth);
    }

    private CheckedStmt readStmt(int tag, int depth) {
        if (depth > MAX_DEPTH) throw invalid("checked body statement nesting exceeds its limit");
        CheckedStmtKind kind = null;
        for (CheckedStmtKind candidate : CheckedStmtKind.values()) if (candidate.tag == tag) kind = candidate;
        if (kind == null) throw invalid("checked body statement kind is unknown");
        CheckedStmt stmt = new CheckedStmt();
        stmt.kind = kind;
        switch (kind) {
            case Let, Var -> {
                stmt.localOrdinal = parseNonNegative(field(), "local ordinal");
                stmt.type = readType();
                stmt.expr = readExpr(depth + 1);
            }
            case Assign -> {
                stmt.localOrdinal = parseNonNegative(field(), "local ordinal");
                stmt.expr = readExpr(depth + 1);
            }
            case AssignField -> {
                stmt.semanticTargetId = field();
                stmt.memberOrdinal = parseNonNegative(field(), "member ordinal");
                stmt.operands.add(readExpr(depth + 1));
                stmt.expr = readExpr(depth + 1);
            }
            case AssignIndex -> {
                stmt.operands.add(readExpr(depth + 1));
                stmt.operands.add(readExpr(depth + 1));
                stmt.expr = readExpr(depth + 1);
            }
            case ExprStmt, Reply -> stmt.expr = readExpr(depth + 1);
            case Return -> {
                String present = field();
                if (present.equals("1")) stmt.expr = readExpr(depth + 1);
                else if (!present.equals("0")) throw invalid("checked body return presence is invalid");
            }
            case If -> {
                stmt.expr = readExpr(depth + 1);
                int thenCount = count("if then statement count");
                for (int i = 0; i < thenCount; i++) stmt.thenBranch.add(readStmt(depth + 1));
                int elseCount = count("if else statement count");
                for (int i = 0; i < elseCount; i++) stmt.elseBranch.add(readStmt(depth + 1));
            }
            case While -> {
                stmt.expr = readExpr(depth + 1);
                int bodyCount = count("while statement count");
                for (int i = 0; i < bodyCount; i++) stmt.body.add(readStmt(depth + 1));
            }
            case For -> {
                stmt.localOrdinal = parseNonNegative(field(), "for binder ordinal");
                stmt.type = readType();
                stmt.expr = readExpr(depth + 1);
                int bodyCount = count("for statement count");
                for (int i = 0; i < bodyCount; i++) stmt.body.add(readStmt(depth + 1));
            }
            case IfJoin -> {
                stmt.localOrdinal = parseNonNegative(field(), "if-join local ordinal");
                stmt.type = readType();
                stmt.expr = readExpr(depth + 1);
                stmt.operands.add(readExpr(depth + 1));
                stmt.operands.add(readExpr(depth + 1));
            }
            case Pass -> { }
            default -> throw invalid("Java checked-body reader does not support statement kind " + kind);
        }
        if (stmt.type == null && stmt.expr != null) stmt.type = stmt.expr.type;
        return stmt;
    }

    public List<CheckedStmt> read() {
        if (!new String(bytes, 0, Math.min(bytes.length, CheckedBodyCodec.HEADER.length()), StandardCharsets.UTF_8)
                .equals(CheckedBodyCodec.HEADER)) throw invalid("checked body header is invalid");
        position = CheckedBodyCodec.HEADER.getBytes(StandardCharsets.UTF_8).length;
        int count = count("top-level statement count");
        if (count == 0) throw invalid("checked body statement sequence is empty");
        List<CheckedStmt> statements = new ArrayList<>(count);
        if (count == 1) {
            String first = field();
            if (first.equals("return")) {
                CheckedStmt returned = new CheckedStmt();
                returned.kind = CheckedStmtKind.Return;
                if (position < bytes.length) returned.expr = readExpr(0);
                returned.type = returned.expr != null ? returned.expr.type : null;
                statements.add(returned);
            } else {
                statements.add(readStmt(parseNonNegative(first, "statement kind"), 0));
            }
        } else {
            for (int i = 0; i < count; i++) statements.add(readStmt(0));
        }
        if (position != bytes.length) throw invalid("checked body has trailing data");
        return statements;
    }

    public boolean isDone() { return position == bytes.length; }
}
