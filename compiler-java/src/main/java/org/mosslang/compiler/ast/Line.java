package org.mosslang.compiler.ast;

import org.eclipse.collections.impl.list.mutable.primitive.IntArrayList;

public class Line {
    public int no = 0;
    public int indent = 0;
    public String text = "";
    public IntArrayList continuationLines = new IntArrayList();
    public String sourceFile = "";

    public Line() {}

    public Line(int no, int indent, String text, IntArrayList continuationLines, String sourceFile) {
        this.no = no;
        this.indent = indent;
        this.text = text != null ? text : "";
        this.continuationLines = continuationLines != null ? IntArrayList.newList(continuationLines) : new IntArrayList();
        this.sourceFile = sourceFile != null ? sourceFile : "";
    }

    public Line(int no, int indent, String text, String sourceFile) {
        this(no, indent, text, new IntArrayList(), sourceFile);
    }

    @Override
    public String toString() {
        return "Line{" + no + ", indent=" + indent + ", text='" + text + "'}";
    }
}
