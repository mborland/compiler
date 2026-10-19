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
  funcs        N small functions, each in its own F_REGION frame

C++ kinds (ECIL v1, header08_cxx; T has a constructor and a destructor that
count and check the order, so main verifies the result at run time):
  cxx_locals       N locals with destructors in one block (N nested cleanup
                   scopes)
  cxx_temps        N temporaries with destructors in one full expression
  cxx_blocks       N nested blocks, each with a local with a destructor
  cxx_meminit      a constructor with N MEMINIT groups (a class of N fields)
  cxx_cond_chain   N nested ?: arms, each with a temporary (one flag-guarded
                   scope, N flags)
  cxx_cond_seq     N sequential ?: operands, each with a temporary
  cxx_array        T a[N] constructed and destroyed with array loops
  cxx_array_nest   N ARRAY_CTOR bodies nested in each other
  cxx_funcs        a TU of N functions: classes with destructors, template-like
                   linkonce_odr instances, guarded local statics and globals
                   with dynamic initialization
"""
import os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
HEADER = os.path.join(HERE, "..", "test", "cirgen", "Inputs", "header.txt")
HEADER_CXX = os.path.join(HERE, "..", "test", "cirgen", "Inputs",
                          "header08_cxx.txt")


def cxx_unit(body, extra_decls=(), n_classes=1, funcs=None, globals_=()):
    """A C++ unit: class T (ids count constructions, the destructor checks
    reverse order), f() = body, main() runs f and checks g_c == 0 and g_bad
    == 0.  body: ECIL lines of f's body (ending in a return)."""
    out = ["frame DECLS"]
    strs = ["T", "g_c", "g_bad", "_ZN1TC2Ev", "_ZN1TC1Ev", "_ZN1TD2Ev",
            "_ZN1TD1Ev", "this", "f", "main"]
    for i, t in enumerate(strs):
        out.append('  STR %d "%s"' % (10 + i, t))
    out += ["  TY_RECORD_DECL 41 0 10", "  TY_RECORD_BODY 41 4 4 4 12 1",
            "  REC_FIELD 1 0 nil nil nil 0", "  TY_PTR 42 41",
            "  TY_FUNC 46 11 0 [42]", "  TY_FUNC 49 1 0 []",
            "  DECL_VAR 301 1 0 1 4 11", "  DECL_VAR 302 1 0 1 4 12",
            "  DECL_FUNC 110 46 0 1 13", "  DECL_FUNC 111 46 0 0 14",
            "  STRUCTOR 111 110 0", "  DECL_FUNC 112 46 0 1 15",
            "  DECL_FUNC 113 46 0 0 16", "  STRUCTOR 113 112 1",
            "  DECL_FUNC 30 49 0 1 18", "  DECL_FUNC 31 49 0 9 19"]
    out += ["  " + x for x in extra_decls]
    out += ["frame REGION"]
    # T::T(): ++g_c; this->id = g_c
    out += ["  FUNC_BEGIN 110 0", "  PARAM 1 42 42 17 1", "  GREF 301",
            "  DUP", "  LOAD 0", "  CONST_INT 1 1", "  ADD 1", "  STORE 0 0",
            "  LREF 1", "  LOAD 0", "  DEREF", "  FIELD 41 0", "  GREF 301",
            "  LOAD 0", "  STORE 0 0", "  FUNC_END 1"]
    # T::~T(): if (id != g_c) g_bad = 1; --g_c
    out += ["  FUNC_BEGIN 112 0", "  PARAM 1 42 42 17 1", "  LREF 1",
            "  LOAD 0", "  DEREF", "  FIELD 41 0", "  LOAD 0", "  GREF 301",
            "  LOAD 0", "  CMP 5", "  IF_BEGIN 0", "  GREF 302",
            "  CONST_INT 1 1", "  STORE 0 0", "  IF_END", "  GREF 301",
            "  DUP", "  LOAD 0", "  CONST_INT 1 1", "  SUB 1", "  STORE 0 0",
            "  FUNC_END 1"]
    out += ["  FUNC_BEGIN 30 0"] + ["  " + x for x in body] + ["  FUNC_END 0"]
    if funcs:
        out += funcs
    out += ["  FUNC_BEGIN 31 0", "  CALL 30 49 0 0", "  DROP",
            "  GREF 301", "  LOAD 0", "  GREF 302", "  LOAD 0", "  BIT_OR 0",
            "  RETURN_STMT true", "  FUNC_END 0"]
    if globals_:
        out += ["frame GLOBALS"] + ["  " + x for x in globals_]
    out += ["frame END", "  END auto 0"]
    return out


def t_ctor(local):
    return ["LREF %d" % local, "ADDR", "CALL 111 46 1 0"]


def t_clean(local):
    return ["CLEANUP_BEGIN 3 nil 0", "LREF %d" % local, "ADDR",
            "CALL 113 46 1 0", "CLEANUP_BODY"]


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
    elif kind == "funcs":
        out = ["frame DECLS", "  TY_FUNC 20 1 0 [1]"]
        for i in range(n):
            g = 100 + i
            out += ["frame REGION", '  STR %d "f%d"' % (1000 + i, i),
                    "  DECL_FUNC %d 20 0 1 %d" % (g, 1000 + i),
                    "  FUNC_BEGIN %d 0" % g, "  PARAM 1 1 1 nil 0",
                    "  LREF 1", "  LOAD 0", "  CONST_INT 1 %d" % i, "  ADD 1",
                    "  RETURN_STMT true", "  FUNC_END 0"]
        out += ["frame END", "  END auto 0"]
    elif kind in ("init_deep", "agg_init"):
        out = ["frame DECLS", '  STR 10 "g"', '  STR 11 "main"',
               '  STR 12 "f"', "  TY_FUNC 21 1 0 []"]
        # record R_i { R_{i+1} m; } ... the innermost holds an int
        base = 100
        for i in range(n + 1):
            out.append("  TY_RECORD_DECL %d 0 nil" % (base + i))
        out.append("  TY_ARRAY %d 1 1 0" % (base + n + 1))
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
    elif kind.startswith("cxx_"):
        out = gen_cxx(kind, n)
    else:
        sys.exit("unknown kind " + kind)
    return out


def gen_cxx(kind, n):
    ret0 = ["CONST_INT 1 0", "RETURN_STMT true"]
    if kind == "cxx_locals":
        ln = []
        for i in range(n):
            ln += ["LOCAL %d 41 nil 4 0" % (1 + i)] + t_ctor(1 + i) + \
                  t_clean(1 + i)
        return cxx_unit(ln + ret0)
    if kind == "cxx_temps":
        ln = ["FULLEXPR_BEGIN"]
        for i in range(n):
            ln += ["LOCAL %d 41 nil 4 2" % (1 + i)] + t_ctor(1 + i) + \
                  t_clean(1 + i)
        return cxx_unit(ln + ["FULLEXPR_END"] + ret0)
    if kind == "cxx_blocks":
        ln = []
        for i in range(n):
            ln += ["BLOCK_BEGIN", "LOCAL %d 41 nil 4 0" % (1 + i)] + \
                  t_ctor(1 + i) + t_clean(1 + i)
        ln += ["BLOCK_END"] * n
        return cxx_unit(ln + ret0)
    if kind == "cxx_meminit":
        decl = ["TY_RECORD_DECL 60 0 nil", "TY_RECORD_BODY 60 %d %d 4 0 %d" %
                (4 * n, 4 * n, n)] + ["REC_FIELD 1 %d nil nil nil 0" % (4 * i)
                                      for i in range(n)]
        decl += ["TY_PTR 61 60", "TY_FUNC 62 11 0 [61]"]
        decl += ["STR 21 \"_ZN1RC2Ev\"", "STR 22 \"_ZN1RC1Ev\"",
                 "DECL_FUNC 120 62 0 1 21", "DECL_FUNC 121 62 0 0 22",
                 "STRUCTOR 121 120 0"]
        ctor = ["FUNC_BEGIN 120 0", "PARAM 1 61 61 17 1"]
        for i in range(n):
            ctor += ["MEMINIT_BEGIN 2 %d 0" % i, "LREF 1", "LOAD 0", "DEREF",
                     "FIELD 60 %d" % i, "CONST_INT 1 %d" % i, "STORE 2 0",
                     "MEMINIT_END"]
        ctor += ["FUNC_END 1"]
        body = ["LOCAL 1 60 nil 4 0", "LREF 1", "ADDR", "CALL 121 62 1 0",
                "LREF 1", "FIELD 60 %d" % (n - 1), "LOAD 0",
                "CONST_INT 1 %d" % (n - 1), "CMP 5", "IF_BEGIN 0",
                "GREF 302", "CONST_INT 1 1", "STORE 0 0", "IF_END"] + ret0
        return cxx_unit(body, decl, funcs=["  " + x for x in ctor])
    if kind == "cxx_cond_chain":
        e = []
        for i in range(n):
            e += ["GREF 303", "LOAD 0", "TO_BOOL", "TERN_BEGIN 1",
                  "LOCAL %d 41 nil 4 2" % (1 + i)] + t_ctor(1 + i) + \
                 t_clean(1 + i) + ["LREF %d" % (1 + i), "FIELD 41 0",
                                   "LOAD 0", "TERN_ELSE"]
        e += ["CONST_INT 1 0"]
        for i in range(n):
            e += ["TERN_END"]
        ln = ["FULLEXPR_BEGIN"] + e + ["DROP", "FULLEXPR_END"] + ret0
        return cxx_unit(ln, ["STR 20 \"g_cond\"", "DECL_VAR 303 1 0 1 4 20", "C_INT 1 1", "VAR_INIT 303"],
                        globals_=[])
    if kind == "cxx_cond_seq":
        ln = ["FULLEXPR_BEGIN", "CONST_INT 1 0"]
        for i in range(n):
            ln += ["GREF 303", "LOAD 0", "TO_BOOL", "TERN_BEGIN 1",
                   "LOCAL %d 41 nil 4 2" % (1 + i)] + t_ctor(1 + i) + \
                  t_clean(1 + i) + ["LREF %d" % (1 + i), "FIELD 41 0",
                                    "LOAD 0", "TERN_ELSE", "CONST_INT 1 0",
                                    "TERN_END", "ADD 1"]
        ln += ["DROP", "FULLEXPR_END"] + ret0
        return cxx_unit(ln, ["STR 20 \"g_cond\"", "DECL_VAR 303 1 0 1 4 20", "C_INT 1 1", "VAR_INIT 303"])
    if kind == "cxx_array":
        decl = ["TY_ARRAY 60 41 %d 0" % n]
        ln = ["LOCAL 1 60 nil 4 0", "LREF 1", "ARRAY_CTOR_BEGIN 41 1",
              "CALL 111 46 1 0", "ARRAY_CTOR_END", "CLEANUP_BEGIN 3 nil 0",
              "LREF 1", "ARRAY_DTOR_BEGIN 41 1", "CALL 113 46 1 0",
              "ARRAY_DTOR_END", "CLEANUP_BODY"] + ret0
        return cxx_unit(ln, decl)
    if kind == "cxx_array_nest":
        decl = ["TY_ARRAY 60 41 2 0"]
        ln = ["LOCAL 1 60 nil 4 0", "LREF 1", "ADDR", "CAST 12 42",
              "CONST_INT 2 2"]
        # each body gets the element pointer; nest with a count of 1
        ln += ["ARRAY_CTOR_BEGIN 41 0"]
        for i in range(n):
            ln += ["CONST_INT 2 1", "ARRAY_CTOR_BEGIN 41 0"]
        ln += ["CALL 111 46 1 0"] + ["ARRAY_CTOR_END"] * (n + 1)
        ln += ["CLEANUP_BEGIN 3 nil 0", "LREF 1", "ARRAY_DTOR_BEGIN 41 1",
               "CALL 113 46 1 0", "ARRAY_DTOR_END", "CLEANUP_BODY"] + ret0
        # the nested loops construct element 0 of the pair n+1 times; the
        # second element is built by a plain call, so the counts balance
        return cxx_unit(ln, decl)
    if kind == "cxx_funcs":
        return gen_cxx_funcs(n)
    sys.exit("unknown kind " + kind)


def gen_cxx_funcs(n):
    """N functions over a few classes: locals with destructors, guarded
    function-local statics, linkonce_odr 'instances', plus globals with
    dynamic initialization (about one in ten functions)."""
    funcs = []
    decls = []
    sid = [30]
    gid = [1000]

    def new_sid(text):
        decls.append('STR %d "%s"' % (sid[0], text))
        sid[0] += 1
        return sid[0] - 1

    def new_gid():
        gid[0] += 1
        return gid[0] - 1

    for i in range(n):
        g = new_gid()
        inst = i % 3 == 0          # a linkonce_odr instance with comdat
        decls.append("DECL_FUNC %d 49 %d %d %d" %
                     (g, 3 if inst else 0, 65 if inst else 1,
                      new_sid("fn%d" % i)))
        sv = new_gid()
        decls.append("DECL_VAR %d 1 1 273 4 %d" %
                     (sv, new_sid("_ZZ2fn%dvE1s" % i)))
        funcs += ["  FUNC_BEGIN %d 0" % g, "  LOCAL 1 41 nil 4 0"]
        funcs += ["  " + x for x in t_ctor(1) + t_clean(1)]
        funcs += ["  STATIC_INIT_BEGIN %d 0" % sv, "  GREF %d" % sv,
                  "  LREF 1", "  FIELD 41 0", "  LOAD 0", "  STORE 2 0",
                  "  STATIC_INIT_END",
                  "  GREF %d" % sv, "  LOAD 0", "  RETURN_STMT true",
                  "  FUNC_END 0"]
    globals_ = []
    for i in range(0, n, 10):
        gv = new_gid()
        decls.append("DECL_VAR %d 1 0 1 4 %d" % (gv, new_sid("gv%d" % i)))
        globals_ += ["STATIC_INIT_BEGIN %d 0" % gv, "GREF %d" % gv,
                     "CONST_INT 1 %d" % i, "STORE 2 0", "STATIC_INIT_END"]
    body = ["CONST_INT 1 0", "RETURN_STMT true"]
    return cxx_unit(body, decls, funcs=funcs, globals_=globals_)


def main():
    kind, n = sys.argv[1], int(sys.argv[2])
    sys.stdout.write(open(HEADER_CXX if kind.startswith("cxx_")
                          else HEADER).read())
    sys.stdout.write("\n".join(gen(kind, n)) + "\n")


main()
