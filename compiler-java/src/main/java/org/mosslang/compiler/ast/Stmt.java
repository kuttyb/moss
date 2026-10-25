package org.mosslang.compiler.ast;

import org.eclipse.collections.api.list.MutableList;
import org.eclipse.collections.impl.list.mutable.FastList;
import org.eclipse.collections.impl.list.mutable.primitive.IntArrayList;

import java.util.List;

public class Stmt {
    public enum Kind {
        Raw, Pass, Assign, Call, Message, Echo, If, Else, While, For, Match, Case,
        Let, Var, Reply, Return,
        Try, Recover, Raise
    }

    public Kind kind = Kind.Raw;
    public int line = 0;
    public int indent = 0;
    public String text = "";
    public String a = "";
    public String b = "";
    public MutableList<String> args = new FastList<>();
    public String messageResult = "";
    public IntArrayList continuationLines = new IntArrayList();
    public String semanticType = "";
    public String sourceFile = "";

    public Stmt() {}

    public Stmt(Kind kind, int line, int indent, String text, String sourceFile) {
        this.kind = kind;
        this.line = line;
        this.indent = indent;
        this.text = text != null ? text : "";
        this.sourceFile = sourceFile != null ? sourceFile : "";
    }

    public Stmt(Kind kind, int line, int indent, String text, String a, String b) {
        this.kind = kind;
        this.line = line;
        this.indent = indent;
        this.text = text != null ? text : "";
        this.a = a != null ? a : "";
        this.b = b != null ? b : "";
    }

    @Override
    public String toString() {
        return "Stmt{" + kind + " line=" + line + " text='" + text + "'}";
    }
}
