#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
"""Print ecil-asm text for a synthetic stress stream.

usage: ecil-stress-gen.py KIND N   (the header comes from test/cirgen/Inputs)

Kinds:
  chain_left   int f(int a) { return a+1+1+...+1; }  N terms, left nested
  chain_right  int f(int a) { return a+(1+(1+...)); } N terms, deep stack
  elseif       if (a==0) r=0; else if (a==1) r=1; ... N deep, unbalanced
  nested_blocks N nested compound statements each with a local
  init_deep    struct S{S1} chain N deep, global with a nested initializer
  agg_init     the same object built by AGG_INIT_* records in a function
  flat         N flat statements r += 1;
  loops        N nested while loops
  switch       one switch with N cases
  ternary      a ? 1 : (a ? 2 : (...)) N deep
  land         a && (a && (...)) N deep
"""
import os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
HEADER = os.path.join(HERE, "..", "test", "cirgen", "Inputs", "header.txt")


def body_func(lines, locals_=("LOCAL 2 1 nil 4 0",)):
    out = ["frame DECLS", '  STR 10 "f"', '  STR 11 "main"',
           "  TY_FUNC 20 1 0 [1]", "  TY_FUNC 21 1 0 []",
           "  DECL_FUNC 30 20 0 1 10", "  DECL_FUNC 31 21 0 9 11",
           "frame REGION", "  FUNC_BEGIN 30 0", "  PARAM 1 1 1 nil 0"]
    out += ["  " + x for x in locals_]
    out += ["  LREF 2", "  CONST_INT 1 0", "  STORE 0 0"]
    out += ["  " + x for x in lines]
    out += ["  LREF 2", "  LOAD 0", "  RETURN_STMT true", "  FUNC_END 0",
            "  FUNC_BEGIN 31 0", "  CONST_INT 1 0", "  RETURN_STMT true",
            "  FUNC_END 0", "frame END", "  END auto 0"]
    return out


def gen(kind, n):
    ln = []
    if kind == "chain_left":
        out = body_func(["LREF 2", "LREF 1", "LOAD 0"] +
                        ["CONST_INT 1 1", "ADD 0"] * n + ["STORE 0 0"])
    elif kind == "chain_right":
        out = body_func(["LREF 2", "LREF 1", "LOAD 0"] +
                        ["CONST_INT 1 1"] * n + ["ADD 0"] * n + ["STORE 0 0"])
    elif kind == "elseif":
        ln = []
        for i in range(n):
            ln += ["LREF 1", "LOAD 0", "CONST_INT 1 %d" % i, "CMP 4",
                   "IF_BEGIN 1", "LREF 2", "CONST_INT 1 %d" % i, "STORE 0 0",
                   "IF_ELSE"]
        ln += ["LREF 2", "CONST_INT 1 -1", "STORE 0 0"]
        ln += ["IF_END"] * n
        out = body_func(ln)
    elif kind == "nested_blocks":
        ln = []
        for i in range(n):
            ln += ["BLOCK_BEGIN", "LOCAL %d 1 nil 4 0" % (10 + i),
                   "LREF %d" % (10 + i), "CONST_INT 1 %d" % i, "STORE 0 0"]
        ln += ["LREF 2", "LREF %d" % (10 + n - 1), "LOAD 0", "STORE 0 0"]
        ln += ["BLOCK_END"] * n
        out = body_func(ln)
    elif kind == "flat":
        ln = ["LREF 2", "DUP", "LOAD 0", "CONST_INT 1 1", "ADD 1",
              "STORE 0 0"] * n
        out = body_func(ln)
    elif kind == "loops":
        ln = []
        for i in range(n):
            ln += ["WHILE_BEGIN", "LREF 1", "LOAD 0", "CONST_INT 1 0", "CMP 2",
                   "WHILE_BODY"]
        ln += ["LREF 2", "CONST_INT 1 1", "STORE 0 0"]
        ln += ["BREAK_STMT"]
        ln += ["WHILE_END"] * n
        out = body_func(ln)
    elif kind == "switch":
        ln = ["LREF 1", "LOAD 0", "SWITCH_BEGIN"]
        for i in range(n):
            ln += ["CASE_VALUE %d" % i, "LREF 2", "CONST_INT 1 %d" % i,
                   "STORE 0 0", "BREAK_STMT"]
        ln += ["SWITCH_END"]
        out = body_func(ln)
    elif kind == "ternary":
        e = []
        for i in range(n):
            e += ["LREF 1", "LOAD 0", "CONST_INT 1 %d" % i, "CMP 4",
                  "TERN_BEGIN 1", "CONST_INT 1 %d" % i, "TERN_ELSE"]
        e += ["CONST_INT 1 -1"] + ["TERN_END"] * n
        out = body_func(["LREF 2"] + e + ["STORE 0 0"])
    elif kind == "land":
        e = []
        for i in range(n):
            e += ["LREF 1", "LOAD 0", "TO_BOOL", "LAND_BEGIN"]
        e += ["CONST_INT 1 1", "TO_BOOL"] + ["LAND_END"] * n
        out = body_func(["LREF 2"] + e + ["CAST 4 1", "STORE 0 0"])
    elif kind in ("init_deep", "agg_init"):
        out = ["frame DECLS", '  STR 10 "g"', '  STR 11 "main"',
               '  STR 12 "f"', "  TY_FUNC 21 1 0 []"]
        # record R_i { R_{i+1} m; } ... the innermost holds an int
        base = 100
        out.append("  TY_ARRAY %d 1 1 0" % (base + n + 1))
        for i in range(n + 1):
            out.append("  TY_RECORD_DECL %d 0 nil" % (base + i))
        for i in range(n, -1, -1):
            inner = base + i + 1 if i < n else 1
            out.append("  TY_RECORD_BODY %d 4 4 4 0 1" % (base + i))
            out.append("  REC_FIELD %d 0 nil nil nil 0" % inner)
        if kind == "init_deep":
            out.append("  DECL_VAR 30 %d 0 1 4 10" % base)
            for i in range(n + 1):
                out += ["  C_AGG_BEGIN %d" % (base + i), "  C_SEL 0"]
            out.append("  C_INT 1 7")
            out += ["  C_AGG_END"] * (n + 1)
            out.append("  VAR_INIT 30")
            out.append("  DECL_FUNC 31 21 0 9 11")
            out += ["frame REGION", "  FUNC_BEGIN 31 0", "  CONST_INT 1 0",
                    "  RETURN_STMT true", "  FUNC_END 0"]
        else:
            out.append("  DECL_FUNC 31 21 0 9 11")
            out += ["frame REGION", "  FUNC_BEGIN 31 0",
                    "  LOCAL 1 %d nil 4 0" % base, "  LREF 1",
                    "  AGG_INIT_BEGIN %d 1" % base]
            for i in range(n):
                out += ["  AGG_SEL 0", "  AGG_INIT_BEGIN %d 1" % (base + i + 1)]
            out += ["  AGG_SEL 0", "  CONST_INT 1 7", "  STORE 0 0"]
            out += ["  AGG_INIT_END"] * (n + 1)
            out += ["  CONST_INT 1 0", "  RETURN_STMT true", "  FUNC_END 0"]
        out += ["frame END", "  END auto 0"]
    else:
        sys.exit("unknown kind " + kind)
    return out


def main():
    kind, n = sys.argv[1], int(sys.argv[2])
    sys.stdout.write(open(HEADER).read())
    sys.stdout.write("\n".join(gen(kind, n)) + "\n")


main()
