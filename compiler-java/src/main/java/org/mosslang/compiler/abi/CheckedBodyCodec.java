package org.mosslang.compiler.abi;

import org.mosslang.compiler.types.TypeKind;
import org.mosslang.compiler.types.TypeRef;

import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.HexFormat;
import java.util.List;

public class CheckedBodyCodec {
    public static final String HEADER = "moss-checked-body-v1\n";

    public static void encodeField(StringBuilder out, String value) {
        String val = value != null ? value : "";
        out.append(val.length()).append(":").append(val);
    }

    public static void encodeType(StringBuilder out, TypeRef type) {
        if (type == null) {
            throw new IllegalArgumentException("checked body node has no TypeRef");
        }
        encodeField(out, String.valueOf(type.kind().ordinal()));
        if (type.kind() == TypeKind.InferenceVar) {
            if (type.varId().sessionId() != 0 || type.varId().ownerId() != 0) {
                throw new IllegalArgumentException("checked body has non-summary type variable");
            }
            encodeField(out, String.valueOf(type.varId().ordinal()));
        } else if (type.kind() == TypeKind.NominalInstance || type.kind() == TypeKind.Enum || type.kind() == TypeKind.Domain) {
            if (type.declaration() == null || type.declaration().getId().isEmpty()) {
                throw new IllegalArgumentException("checked body has unresolved declaration");
            }
            encodeField(out, type.declaration().getId());
        } else {
            encodeField(out, type.name());
        }

        encodeField(out, String.valueOf(type.typeArgs().size()));
        for (TypeRef arg : type.typeArgs()) {
            encodeType(out, arg);
        }

        encodeField(out, String.valueOf(type.fieldSubstitutions().size()));
        for (TypeRef.FieldSub fs : type.fieldSubstitutions()) {
            encodeField(out, fs.name());
            encodeType(out, fs.type());
        }
    }

    public static void encodeExpr(StringBuilder out, CheckedExpr expr) {
        if (expr == null || expr.type == null) {
            throw new IllegalArgumentException("checked body expr has no TypeRef");
        }
        encodeField(out, String.valueOf(expr.kind.tag));
        encodeType(out, expr.type);

        switch (expr.kind) {
            case Parameter -> {
                encodeField(out, String.valueOf(expr.ordinal));
            }
            case IntegerLiteral -> {
                encodeField(out, expr.literalValue);
            }
            case FloatLiteral, StringLiteral -> {
                encodeField(out, expr.literalValue);
            }
            case BoolLiteral -> {
                encodeField(out, "true".equals(expr.literalValue) ? "1" : "0");
            }
            case LocalRead -> {
                encodeField(out, String.valueOf(expr.ordinal));
            }
            case Add -> {
                encodeField(out, "2");
                for (CheckedExpr child : expr.operands) {
                    encodeExpr(out, child);
                }
            }
            case Unary -> {
                encodeField(out, String.valueOf(expr.unaryOp.tag));
                encodeExpr(out, expr.operands.get(0));
            }
            case Binary -> {
                encodeField(out, String.valueOf(expr.binaryOp.tag));
                encodeExpr(out, expr.operands.get(0));
                encodeExpr(out, expr.operands.get(1));
            }
            case Field -> {
                encodeField(out, expr.semanticTargetId);
                encodeField(out, String.valueOf(expr.memberOrdinal));
            }
            default -> {
                encodeField(out, expr.literalValue != null ? expr.literalValue : "");
            }
        }
    }

    public static void encodeStmt(StringBuilder out, CheckedStmt stmt) {
        if (stmt == null) return;
        encodeField(out, String.valueOf(stmt.kind.tag));
        encodeField(out, String.valueOf(stmt.localOrdinal));

        if (stmt.expr != null) {
            encodeField(out, "1");
            encodeExpr(out, stmt.expr);
        } else {
            encodeField(out, "0");
        }

        encodeField(out, String.valueOf(stmt.operands.size()));
        for (CheckedExpr op : stmt.operands) {
            encodeExpr(out, op);
        }

        encodeField(out, String.valueOf(stmt.body.size()));
        for (CheckedStmt s : stmt.body) {
            encodeStmt(out, s);
        }
    }

    public static String encodeCheckedBodyV1(List<CheckedStmt> statements) {
        StringBuilder out = new StringBuilder();
        out.append(HEADER);
        encodeField(out, String.valueOf(statements != null ? statements.size() : 0));
        if (statements != null) {
            for (CheckedStmt s : statements) {
                encodeStmt(out, s);
            }
        }
        return out.toString();
    }

    public static String computeCheckedBodyHashV1(List<CheckedStmt> statements) {
        String encoded = encodeCheckedBodyV1(statements);
        try {
            MessageDigest md = MessageDigest.getInstance("SHA-256");
            byte[] digest = md.digest(encoded.getBytes(StandardCharsets.UTF_8));
            return "body-semantic-v1:sha256:" + HexFormat.of().formatHex(digest);
        } catch (NoSuchAlgorithmException e) {
            throw new RuntimeException("SHA-256 not available", e);
        }
    }
}
