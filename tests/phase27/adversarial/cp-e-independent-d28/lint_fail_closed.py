#!/usr/bin/env python3
"""In-memory mutation tests for function+reader API exemptions."""
import importlib.util
from pathlib import Path

ROOT=Path(__file__).resolve().parents[4]
script=ROOT/"tests/tooling/check_phase27_string_debt.py"
spec=importlib.util.spec_from_file_location("debt_lint",script)
lint=importlib.util.module_from_spec(spec); spec.loader.exec_module(lint)
original=lint.read_source
path="src/semantic_checker.cpp"

def remove_function(relative,commit=None):
    text=original(relative,commit)
    if relative!=path: return text
    start=text.index("bool phase20_is_nontransferable_capability(")
    brace=text.index("{",start); depth=0
    for i in range(brace,len(text)):
        if text[i]=="{": depth+=1
        elif text[i]=="}":
            depth-=1
            if depth==0: return text[:start]+text[i+1:]
    raise AssertionError("function body not balanced")

lint.read_source=remove_function
try: lint.sites()
except SystemExit as e:
    assert "missing exemption function" in str(e)
    print("PASS: disappeared exemption function fails closed:",e)
else: raise SystemExit("FAIL: disappeared function was accepted")

def duplicate_reader(relative,commit=None):
    text=original(relative,commit)
    if relative!=path: return text
    start=text.index("bool phase20_is_nontransferable_capability(")
    brace=text.index("{",start); depth=0; end=None
    for i in range(brace,len(text)):
        if text[i]=="{": depth+=1
        elif text[i]=="}":
            depth-=1
            if depth==0: end=i; break
    if end is None: raise AssertionError("function body not balanced")
    return text[:end]+"\n  (void)split_top_level(\"x\", ',');"+text[end:]

lint.read_source=duplicate_reader
try: lint.sites()
except SystemExit as e:
    assert "gained multiple reader sites" in str(e)
    print("PASS: added reader site fails closed:",e)
else: raise SystemExit("FAIL: added site was accepted")
