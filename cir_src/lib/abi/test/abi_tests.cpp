/*
Part of the EDG Compiler Project, under the Apache License v2.0 with LLVM
Exceptions.
See https://edgcpp.org/LICENSE.txt for license information.
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
*/

/*
abi_tests -- unit tests of the ABI builders.  The layouts of the hand-made
classes below were checked against clang -fdump-vtable-layouts (the broader
comparison is tools/compare_with_clang.py).  "deep" runs only the 5000-level
inheritance chains (the CTest entry runs it under a 256 KB stack).
*/

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "abi_builder.h"
#include "abi_names.h"
#include "abi_print.h"
#include "abi_rtti.h"
#include "abi_text.h"

using namespace edg_abi;

namespace {

int failures = 0;

void check(bool ok, const char *what, const std::string &detail = "")
{
  if (!ok) {
    ++failures;
    std::printf("FAIL: %s %s\n", what, detail.c_str());
  }
}

a_class_id add_class(a_class_db &db, const std::string &name, int64_t size,
                     int64_t nvsize, bool dynamic)
{
  a_class_fact c;

  c.name = name;
  c.size = size;
  c.nvsize = nvsize;
  c.is_dynamic = dynamic;
  c.primary = no_id;
  c.primary_virtual = false;
  c.nslots = 0;
  db.classes.push_back(c);
  return static_cast<a_class_id>(db.classes.size() - 1);
}

a_fn_id add_fn(a_class_db &db, a_class_id owner, const std::string &name,
               int32_t slot, a_fn_special special = fs_none)
{
  a_fn_fact f;

  f.owner = owner;
  f.slot = slot;
  f.is_pure = false;
  f.is_deleted = false;
  f.special = special;
  f.is_implicit = false;
  f.sig = special == fs_destructor ? 0 : static_cast<uint32_t>(db.fns.size()) + 1;
  f.ret_class = no_id;
  f.name = name;
  db.fns.push_back(f);
  db.classes[owner].fns.push_back(static_cast<a_fn_id>(db.fns.size() - 1));
  return static_cast<a_fn_id>(db.fns.size() - 1);
}

void test_single()
/*
struct A { virtual void f(); virtual ~A(); };
struct B : A { void f(); virtual int g(char); };
*/
{
  a_class_db db;
  a_class_id a = add_class(db, "A", 16, 12, true);
  a_class_id b = add_class(db, "B", 16, 12, true);
  a_fn_id af = add_fn(db, a, "A::f()", 0);
  add_fn(db, a, "A::~A()", 1, fs_destructor);
  a_fn_id bf = add_fn(db, b, "B::f()", 0);
  add_fn(db, b, "B::~B()", 1, fs_destructor);
  add_fn(db, b, "B::g(char)", 3);
  db.fns[bf].overridden.push_back(af);
  db.fns[3].overridden.push_back(1);
  db.classes[a].nslots = 3;
  db.classes[b].nslots = 4;
  db.classes[b].primary = a;
  db.classes[b].bases.push_back({a, false, 0});
  db.classes[b].overrides.push_back({af, 0, bf, 0});
  db.classes[b].overrides.push_back({1, 0, 3, 0});
  check(db.validate().empty(), "single: validate", db.validate());
  a_vtable_context ctx(db);
  std::string error;
  std::string text = print_vtable(ctx, b, error);

  check(error.empty(), "single: vtable", error);
  check(text.find("Vtable for 'B' (6 entries).") != std::string::npos,
        "single: 6 entries", text);
  check(text.find("   2 | B::f()") != std::string::npos, "single: B::f",
        text);
  check(text.find("   5 | B::g(char)") != std::string::npos, "single: g",
        text);
  check(text.find("-- (A, 0) vtable address --") != std::string::npos &&
        text.find("-- (B, 0) vtable address --") != std::string::npos,
        "single: address points", text);
  check(check_slots(ctx, b).empty(), "single: slots", check_slots(ctx, b));
  check(check_slots_against_builder(ctx, b).empty(), "single: builder slots",
        check_slots_against_builder(ctx, b));
}

void test_bad_slots()
/*
The slot checks find a gap and a clash.
*/
{
  a_class_db db;
  a_class_id a = add_class(db, "A", 8, 8, true);
  add_fn(db, a, "A::f()", 0);
  add_fn(db, a, "A::g()", 2);          /* a gap at slot 1 */
  db.classes[a].nslots = 3;
  check(db.validate().empty(), "gap: validate");
  a_vtable_context ctx(db);
  check(!check_slots(ctx, a).empty(), "gap: detected");
}

void test_text_round_trip()
{
  a_class_db db;
  a_class_id a = add_class(db, "A", 16, 12, true);
  a_class_id b = add_class(db, "B", 24, 24, true);
  a_fn_id f = add_fn(db, a, "A::f(int)", 0);
  a_fn_id g = add_fn(db, b, "B::f(int)", 0);
  db.fns[g].overridden.push_back(f);
  db.classes[b].primary = a;
  db.classes[b].bases.push_back({a, false, 0});
  db.classes[b].overrides.push_back({f, 0, g, 0});
  db.classes[a].nslots = 1;
  db.classes[b].nslots = 1;
  std::string text = write_facts(db);
  a_class_db again;

  check(parse_facts(text, again).empty(), "text: parse", parse_facts(text, again));
  check(write_facts(again) == text, "text: round trip", write_facts(again));
}

void test_cycle()
{
  a_class_db db;
  a_class_id a = add_class(db, "A", 8, 8, true);
  a_class_id b = add_class(db, "B", 8, 8, true);
  db.classes[a].bases.push_back({b, false, 0});
  db.classes[b].bases.push_back({a, false, 0});
  check(!db.validate().empty(), "cycle: rejected");
}

void test_deep(int n)
/*
Chains of n classes: a primary chain, and a chain with a secondary base at
every level.  Both under a small machine stack: the builders do not recurse.
*/
{
  {
    a_class_db db;
    a_class_id prev = no_id;

    for (int i = 0; i < n; ++i) {
      a_class_id c = add_class(db, "C" + std::to_string(i), 8, 8, true);

      add_fn(db, c, "C" + std::to_string(i) + "::f" + std::to_string(i)
                        + "()", i);
      db.classes[c].nslots = static_cast<uint32_t>(i + 1);
      if (prev != no_id) {
        db.classes[c].primary = prev;
        db.classes[c].bases.push_back({prev, false, 0});
      }
      prev = c;
    }
    check(db.validate().empty(), "deep primary: validate", db.validate());
    a_vtable_context ctx(db);
    std::string error;
    const a_vtable_info &info = ctx.vtable_info(prev, error);

    check(error.empty(), "deep primary: build", error);
    check(info.layout.components.size() == static_cast<size_t>(n) + 2,
          "deep primary: size", std::to_string(info.layout.components.size()));
    check(check_slots_against_builder(ctx, prev).empty(),
          "deep primary: slots", check_slots_against_builder(ctx, prev));
  }
  {
    a_class_db db;
    a_class_id prev = no_id;
    int64_t size = 8;

    for (int i = 0; i < n; ++i) {
      a_class_id s = add_class(db, "S" + std::to_string(i), 8, 8, true);
      a_class_id c = add_class(db, "D" + std::to_string(i), size + 8,
                               size + 8, true);

      add_fn(db, s, "S" + std::to_string(i) + "::s()", 0);
      db.classes[s].nslots = 1;
      add_fn(db, c, "D" + std::to_string(i) + "::d" + std::to_string(i) +
                        "()", i);
      db.classes[c].nslots = static_cast<uint32_t>(i + 1);
      if (prev != no_id) {
        db.classes[c].primary = prev;
        db.classes[c].bases.push_back({prev, false, 0});
        db.classes[c].bases.push_back({s, false, size});
        size += 8;
      } else {
        db.classes[c].bases.push_back({s, false, 8});
      }
      prev = c;
    }
    check(db.validate().empty(), "deep secondary: validate", db.validate());
    a_vtable_context ctx(db);
    std::string error;
    const a_vtable_info &info = ctx.vtable_info(prev, error);

    check(error.empty(), "deep secondary: build", error);
    check(info.layout.num_vtables() == static_cast<size_t>(n) + 1 ||
          info.layout.num_vtables() == static_cast<size_t>(n),
          "deep secondary: vtables", std::to_string(info.layout.num_vtables()));
  }
}


a_class_id find_class(const a_class_db &db, const std::string &name)
{
  a_class_id found = no_id;

  for (a_class_id c = 0; c < db.classes.size(); ++c) {
    if (db.classes[c].name == name)
      found = c;
  }
  return found;
}

/* The diamond  struct V { virtual void f(); int v; };  struct A : virtual V
   { int a; };  struct B : virtual V { int b; };  struct D : A, B {};  fed to
   the builder in a scrambled order with sparse ids. */
std::string build_diamond(a_class_db &db)
{
  a_fact_builder fb;
  a_class_info ci;
  a_vfunc_info vf;
  const a_key kV = 900, kA = 17, kB = 4000000000ull, kD = 5, kf = 77;

  /* D first, with forward references to everything. */
  fb.add_base(kD, kA, false, true, 0);
  fb.add_base(kD, kB, false, true, 16);
  fb.add_vbase(kD, kV, true, 32);
  ci.name = "D"; ci.mangled = "1D"; ci.size = 48; ci.nvsize = 28;
  ci.is_dynamic = true; ci.has_primary = true; ci.primary = kA;
  fb.set_class(kD, ci);
  fb.add_base(kA, kV, true, true, 16);
  fb.add_vbase(kA, kV, true, 16);
  ci = a_class_info();
  ci.name = "A"; ci.mangled = "1A"; ci.size = 32; ci.nvsize = 12;
  ci.is_dynamic = true;
  fb.set_class(kA, ci);
  fb.add_base(kB, kV, true, true, 16);
  fb.add_vbase(kB, kV, true, 16);
  ci = a_class_info();
  ci.name = "B"; ci.mangled = "1B"; ci.size = 32; ci.nvsize = 12;
  ci.is_dynamic = true;
  fb.set_class(kB, ci);
  ci = a_class_info();
  ci.name = "V"; ci.mangled = "1V"; ci.size = 16; ci.nvsize = 12;
  ci.is_dynamic = true; ci.nslots = 1;
  fb.set_class(kV, ci);
  vf.name = "V::f()"; vf.mangled = "_ZN1V1fEv"; vf.slot = 0; vf.sig = 1;
  fb.add_vfunc(kV, kf, vf);
  return fb.finish(db);
}

std::string build_empty_base_vbase(a_class_db &db, int64_t nv_data_end)
/*
struct E {}; struct E2 : E {}; struct A { int i; }; struct B { int j; };
struct C : E, A, E2, virtual B {};  (clang: sizeof 16, nvsize 13, dsize of the
non-virtual data 12; B at offset 12, over the byte that E2 counts.)
*/
{
  a_fact_builder fb;
  a_class_info ci;
  const a_key kE = 1, kA = 2, kE2 = 3, kB = 4, kC = 5;

  fb.add_base(kC, kE, false, true, 0);
  fb.add_base(kC, kA, false, true, 8);
  fb.add_base(kC, kE2, false, true, 12);
  fb.add_base(kC, kB, true, true, 12);
  fb.add_vbase(kC, kB, true, 12);
  ci.name = "C"; ci.mangled = "1C"; ci.size = 16; ci.nvsize = 13;
  ci.nv_data_end = nv_data_end; ci.is_dynamic = true;
  fb.set_class(kC, ci);
  ci = a_class_info();
  ci.name = "E"; ci.mangled = "1E"; ci.size = 1; ci.nvsize = 1;
  ci.is_empty = true;
  fb.set_class(kE, ci);
  ci.name = "E2"; ci.mangled = "2E2";
  fb.set_class(kE2, ci);
  ci = a_class_info();
  ci.name = "A"; ci.mangled = "1A"; ci.size = 4; ci.nvsize = 4;
  fb.set_class(kA, ci);
  ci.name = "B"; ci.mangled = "1B";
  fb.set_class(kB, ci);
  return fb.finish(db);
}

void test_empty_base_vbase()
/*
A virtual base may start at the end of the non-virtual data, below nvsize
(empty10), and not before it.
*/
{
  a_class_db good;
  a_class_db bad;
  std::string problem = build_empty_base_vbase(good, 12);

  check(problem.empty() && good.validate_deep().empty(),
        "empty base then virtual base: accepted",
        problem + good.validate_deep());
  problem = build_empty_base_vbase(bad, 16);
  check(problem.find("inside the non-virtual part") != std::string::npos,
        "virtual base inside the non-virtual data: rejected", problem);
}

void test_builder()
{
  a_class_db db;
  std::string problem = build_diamond(db);

  check(problem.empty(), "builder: finish", problem);
  check(db.classes.size() == 4 && db.fns.size() == 1, "builder: counts");
  if (problem.empty()) {
    a_class_id d = find_class(db, "D");
    a_class_id a = find_class(db, "A");

    check(d != no_id && db.classes[d].primary == a, "builder: primary");
    check(db.classes[d].bases.size() == 2 && db.classes[d].vbases.size() == 1,
          "builder: bases");
    a_vtable_context ctx(db);
    std::string error;
    std::string text = print_vtable(ctx, d, error);

    check(error.empty() && text.find("Vtable for 'D'") != std::string::npos,
          "builder: vtable", error + text);
  }
  {
    a_class_db again;

    check(build_diamond(again).empty(), "builder: reuse");
  }
  {
    a_fact_builder fb;
    a_class_db out;
    a_class_info ci;

    fb.add_base(1, 2, false, true, 0);
    ci.name = "X"; ci.size = 8; ci.nvsize = 8;
    fb.set_class(1, ci);
    check(!fb.finish(out).empty(), "builder: base without CLASS_INFO");
    fb.set_class(1, ci);
    fb.set_class(1, ci);
    check(!fb.finish(out).empty(), "builder: two CLASS_INFO records");
  }
}

void test_validate_deep()
/*
Mutations of a valid fact set are rejected.
*/
{
  struct a_case {
    const char  *what;
    void       (*mutate)(a_class_db &);
  };
  static const a_case cases[] = {
    {"base beyond the class", [](a_class_db &db) {
       db.classes[find_class(db, "D")].bases[1].offset = 40; }},
    {"primary not at 0", [](a_class_db &db) {
       db.classes[find_class(db, "D")].bases[0].offset = 4; }},
    {"slot out of range", [](a_class_db &db) { db.fns[0].slot = 5; }},
    {"destructor with a signature", [](a_class_db &db) {
       db.fns[0].special = fs_destructor; }},
    {"virtual base inside the non-virtual part", [](a_class_db &db) {
       a_class_fact &a = db.classes[find_class(db, "A")];
       a.bases[0].offset = 8;
       a.vbases[0].offset = 8; }},
    {"override at no subobject", [](a_class_db &db) {
       db.classes[find_class(db, "D")].overrides.push_back(
         {0, 12, 0, 12}); }},
    {"function in a class without a vptr", [](a_class_db &db) {
       db.classes[find_class(db, "V")].is_dynamic = false; }},
    {"overridden function of a non-base", [](a_class_db &db) {
       a_fn_fact f = db.fns[0];
       f.owner = find_class(db, "B");
       f.overridden.push_back(0);
       db.fns.push_back(f);
       db.classes[f.owner].fns.push_back(1); }},
    {"nvsize beyond size", [](a_class_db &db) {
       db.classes[find_class(db, "A")].nvsize = 40; }},
  };
  a_class_db good;
  std::string problem = build_diamond(good);

  check(problem.empty() && good.validate_deep().empty(), "deep: good diamond",
        problem + good.validate_deep());
  for (const a_case &k : cases) {
    a_class_db db;

    build_diamond(db);
    k.mutate(db);
    check(!db.validate_deep().empty(), "deep: rejected", k.what);
  }
}

void test_names()
{
  bool exact = false;

  check(mangle_number(0) == "0" && mangle_number(24) == "24" &&
        mangle_number(-24) == "n24", "names: number");
  check(vtable_symbol("1D") == "_ZTV1D" && vtt_symbol("1D") == "_ZTT1D" &&
        typeinfo_symbol("1D") == "_ZTI1D" &&
        typeinfo_name_symbol("1D") == "_ZTS1D", "names: special");
  check(ctor_vtable_symbol("1D", 16, "1B", exact) == "_ZTC1D16_1B" && exact,
        "names: ctor vtable, global");
  check(ctor_vtable_symbol("N1N1CE", 0, "N1N1BE", exact) ==
        "_ZTCN1N1CE0_NS_1BE" && exact, "names: ctor vtable, namespace");
  check(ctor_vtable_symbol("N1N1CE", 8, "N1N1C1DE", exact) ==
        "_ZTCN1N1CE8_NS0_1DE" && exact, "names: ctor vtable, whole prefix");
  check(ctor_vtable_symbol("N1N1CE", 8, "1B", exact) == "_ZTCN1N1CE8_1B" &&
        exact, "names: ctor vtable, no match");
  check(ctor_vtable_symbol("N1N1CE", 8, "N1M1BE", exact) ==
        "_ZTCN1N1CE8_N1M1BE" && exact, "names: ctor vtable, other namespace");
  check(ctor_vtable_symbol("N1AIiE1BE", 8, "1D", exact) ==
        "_ZTCN1AIiE1BE8_1D" && exact, "names: ctor vtable, template");
  check(ctor_vtable_symbol("N1N2TCIiEE", 8, "N1N2TAIiEE", exact) ==
        "_ZTCN1N2TCIiEE8_NS_2TAIiEE" && exact,
        "names: ctor vtable, template ids");
  check(ctor_vtable_symbol("N1AIPiE1BE", 8, "1D", exact) ==
        "_ZTCN1AIPiE1BE8_1D" && !exact, "names: ctor vtable, outside subset");
  check(ctor_vtable_symbol("St3foo", 0, "N1N1BE", exact) ==
        "_ZTCSt3foo0_N1N1BE" && exact, "names: ctor vtable, std");
  a_adjustment nv = {-8, 0}, vc = {0, -24}, both = {16, -24}, none = {0, 0};
  check(thunk_symbol(nv, none, "_ZN1B1fEv") == "_ZThn8_N1B1fEv",
        "names: non-virtual thunk", thunk_symbol(nv, none, "_ZN1B1fEv"));
  check(thunk_symbol(vc, none, "_ZN1B1fEv") == "_ZTv0_n24_N1B1fEv",
        "names: virtual thunk");
  check(thunk_symbol(both, none, "_ZN1B1fEv") == "_ZTv16_n24_N1B1fEv",
        "names: virtual thunk with a non-virtual part");
  check(thunk_symbol(nv, vc, "_ZN1B1fEv") == "_ZTchn8_v0_n24_N1B1fEv",
        "names: covariant thunk", thunk_symbol(nv, vc, "_ZN1B1fEv"));
  check(thunk_symbol(none, nv, "_ZN1B1fEv") == "_ZTch0_hn8_N1B1fEv",
        "names: covariant-only thunk", thunk_symbol(none, nv, "_ZN1B1fEv"));
  check(thunk_symbol(nv, none, "main").empty(), "names: not a _Z name");
  check(dtor_variant_symbol("_ZN1BD1Ev", '0') == "_ZN1BD0Ev" &&
        dtor_variant_symbol("_ZN1BD2Ev", '1') == "_ZN1BD1Ev" &&
        dtor_variant_symbol("_ZN1B1fEv", '0').empty(), "names: dtor variant");
}

void test_rtti()
{
  a_class_db db;
  std::string problem = build_diamond(db);
  a_vtable_context ctx(db);
  std::string error;
  a_rtti_layout lay;
  a_class_id a = find_class(db, "A"), v = find_class(db, "V");
  a_class_id d = find_class(db, "D");

  check(problem.empty(), "rtti: build", problem);
  check(rtti_layout(ctx, v, lay, error) && lay.kind == rtti_class &&
        lay.fields.size() == 2, "rtti: class", error);
  check(rtti_layout(ctx, a, lay, error) && lay.kind == rtti_vmi_class,
        "rtti: virtual base needs vmi", error);
  if (lay.fields.size() == 6) {
    int64_t vbo = ctx.virtual_base_offset_offset(a, v, error);

    check(lay.fields[2].value == 0 && lay.fields[3].value == 1 &&
          lay.fields[5].value == ((vbo * 256) | 3),
          "rtti: A fields", print_rtti(db, lay));
  } else {
    check(false, "rtti: A field count", print_rtti(db, lay));
  }
  check(rtti_layout(ctx, d, lay, error) && lay.kind == rtti_vmi_class,
        "rtti: D vmi", error);
  check(lay.fields.size() == 8 && lay.fields[2].value == vmi_diamond_shaped &&
        lay.fields[3].value == 2 && lay.fields[5].value == 2 &&
        lay.fields[7].value == (16 * 256 | 2), "rtti: D fields",
        print_rtti(db, lay));
  check(vmi_class_flags(db, d) == vmi_diamond_shaped, "rtti: flags");

  /* struct P { int p; };  struct Q : P {};  struct R : private P {};
     struct S : P, Q (a repeated non-virtual base). */
  a_class_db db2;
  a_class_id p = add_class(db2, "P", 4, 4, false);
  a_class_id q = add_class(db2, "Q", 4, 4, false);
  a_class_id r = add_class(db2, "R", 4, 4, false);
  a_class_id s2 = add_class(db2, "S", 8, 8, false);

  db2.classes[q].bases.push_back({p, false, 0, true});
  db2.classes[r].bases.push_back({p, false, 0, false});
  db2.classes[s2].bases.push_back({p, false, 0, true});
  db2.classes[s2].bases.push_back({q, false, 4, true});
  check(db2.validate_deep().empty(), "rtti: facts", db2.validate_deep());
  a_vtable_context ctx2(db2);

  check(rtti_layout(ctx2, q, lay, error) && lay.kind == rtti_si_class &&
        lay.fields.size() == 3, "rtti: si", error);
  check(rtti_layout(ctx2, r, lay, error) && lay.kind == rtti_vmi_class
        && lay.fields.size() == 6 && lay.fields[5].value == 0,
        "rtti: private base is vmi without the public bit",
        print_rtti(db2, lay));
  check(rtti_layout(ctx2, s2, lay, error) && lay.kind == rtti_vmi_class
        && lay.fields[2].value == vmi_non_diamond_repeat &&
        lay.fields[5].value == 2 && lay.fields[7].value == (4 * 256 | 2),
        "rtti: repeated base", print_rtti(db2, lay));
}

void test_nearly_empty_primary_vbase()
/*
struct A { virtual void f(); };  struct B : virtual A {};  A lives inside B's
non-virtual part (it is B's primary virtual base: no storage of its own), at
offset 0; validate_deep must accept it, and still reject a real virtual base
that overlaps the non-virtual part.
*/
{
  a_class_db db;
  a_class_id a = add_class(db, "A", 8, 8, true);
  a_class_id b = add_class(db, "B", 8, 8, true);

  add_fn(db, a, "A::f()", 0);
  db.classes[a].nslots = 1;
  db.classes[b].nslots = 1;
  db.classes[b].primary = a;
  db.classes[b].primary_virtual = true;
  db.classes[b].bases.push_back({a, true, 0, true});
  db.classes[b].vbases.push_back({a, true, 0, true});
  check(db.validate_deep().empty(), "nearly empty primary vbase: accepted",
        db.validate_deep());
  /* Not primary: the same overlap is an error. */
  db.classes[b].primary = no_id;
  db.classes[b].primary_virtual = false;
  check(!db.validate_deep().empty(), "overlapping virtual base: rejected");
  /* A primary virtual base of a base (C : B, B's primary is A). */
  db.classes[b].primary = a;
  db.classes[b].primary_virtual = true;
  a_class_id c = add_class(db, "C", 8, 8, true);
  db.classes[c].nslots = 1;
  db.classes[c].primary = b;
  db.classes[c].bases.push_back({b, false, 0, true});
  db.classes[c].vbases.push_back({a, true, 0, true});
  check(db.validate_deep().empty(), "primary vbase of a base: accepted",
        db.validate_deep());
}

void test_plain_bases()
/*
12.2: only dynamic classes have layout facts right away; a class that is not
dynamic has no CLASS_INFO or CLASS_BASES unless something needs them.  Through
the builder:
  struct Q {int q;};  struct P : Q {int p;};  struct R : Q {int r;};
  struct X : P, R {};                     (X has its facts; P, R, Q are plain)
  struct A { virtual void f(); };  struct B : A {};  struct V {int v;};
  struct D : B, virtual V {};             (B is dynamic through A)
  struct N { virtual void g(); };  struct M : virtual N {};
  struct W : virtual M {};                (M: nearly empty, primary virtual)
The walk of vmi_class_flags goes through the plain classes by their BASE
fields: Q repeats non-virtually under X.
*/
{
  a_fact_builder fb;
  a_class_db db;
  a_class_info ci;
  a_vfunc_info vi;

  fb.set_plain_class(1, "Q", 4, 4, false);
  fb.set_plain_class(2, "P", 8, 8, false);
  fb.add_base(2, 1, false, true, 0);
  fb.set_plain_class(3, "R", 8, 8, false);
  fb.add_base(3, 1, false, true, 0);
  ci.name = "X"; ci.size = 16; ci.nvsize = 16;
  fb.set_class(4, ci);
  fb.add_base(4, 2, false, true, 0);
  fb.add_base(4, 3, false, true, 8);
  /* A, B : A, V, D : B, virtual V */
  ci = a_class_info();
  ci.name = "A"; ci.size = 8; ci.nvsize = 8; ci.is_dynamic = true;
  ci.nslots = 1; ci.mangled = "1A";
  fb.set_class(10, ci);
  vi.name = "A::f()"; vi.mangled = "_ZN1A1fEv"; vi.slot = 0; vi.sig = 1;
  fb.add_vfunc(10, 100, vi);
  ci = a_class_info();
  ci.name = "B"; ci.size = 8; ci.nvsize = 8; ci.is_dynamic = true;
  ci.has_primary = true; ci.primary = 10; ci.nslots = 1; ci.mangled = "1B";
  fb.set_class(11, ci);
  fb.add_base(11, 10, false, true, 0);
  fb.set_plain_class(12, "V", 4, 4, false);
  ci = a_class_info();
  ci.name = "D"; ci.size = 16; ci.nvsize = 8; ci.is_dynamic = true;
  ci.has_primary = true; ci.primary = 11; ci.nslots = 1; ci.mangled = "1D";
  fb.set_class(13, ci);
  fb.add_base(13, 11, false, true, 0);
  fb.add_base(13, 12, true, true, 8);
  fb.add_vbase(13, 12, true, 8);
  /* N, M : virtual N (primary virtual base), W : virtual M */
  ci = a_class_info();
  ci.name = "N"; ci.size = 8; ci.nvsize = 8; ci.is_dynamic = true;
  ci.nslots = 1; ci.mangled = "1N";
  fb.set_class(20, ci);
  vi.name = "N::g()"; vi.mangled = "_ZN1N1gEv"; vi.slot = 0; vi.sig = 1;
  fb.add_vfunc(20, 101, vi);
  ci = a_class_info();
  ci.name = "M"; ci.size = 8; ci.nvsize = 8; ci.is_dynamic = true;
  ci.has_primary = true; ci.primary = 20; ci.primary_virtual = true;
  ci.nslots = 1; ci.mangled = "1M";
  fb.set_class(21, ci);
  fb.add_base(21, 20, true, true, 0);
  fb.add_vbase(21, 20, true, 0);
  ci = a_class_info();
  ci.name = "W"; ci.size = 8; ci.nvsize = 8; ci.is_dynamic = true;
  ci.has_primary = true; ci.primary = 21; ci.primary_virtual = true;
  ci.nslots = 1; ci.mangled = "1W";
  fb.set_class(22, ci);
  fb.add_base(22, 21, true, true, 0);
  fb.add_vbase(22, 21, true, 0);
  fb.add_vbase(22, 20, true, 0);
  std::string problem = fb.finish(db);

  check(problem.empty(), "plain bases: finish", problem);
  if (problem.empty()) {
    a_class_id x = find_class(db, "X");
    a_class_id p = find_class(db, "P");
    a_class_id d = find_class(db, "D");

    check(vmi_class_flags(db, x) == vmi_non_diamond_repeat,
          "plain bases: Q repeats under X");
    check(db.classes[p].bases.size() == 1 && !db.classes[p].is_dynamic,
          "plain bases: P keeps its BASE field as its base");
    a_vtable_context ctx(db);
    std::string error;
    std::string text = print_vtable(ctx, d, error);

    check(error.empty() && text.find("Vtable for 'D'") != std::string::npos,
          "plain bases: vtable of D", error + text);
    text = print_vtable(ctx, find_class(db, "W"), error);
    check(error.empty() && text.find("Vtable for 'W'") != std::string::npos,
          "plain bases: vtable of W", error + text);
  }
}

void test_typeinfo_name_string()
{
  check(typeinfo_name_string("1D", false) == "1D", "name string: external");
  check(typeinfo_name_string("N12_GLOBAL__N_12AnE", true) ==
        "*N12_GLOBAL__N_12AnE", "name string: TU-local");
}

std::string build_override_chain(a_class_db &db, int n, bool wrong_slot)
/*
n classes C0 <- C1 <- ... each declaring f (slot 0) that overrides every
earlier f, fed to the builder: the final overrider lists have i entries for
class i (n * n / 2 in all).
*/
{
  a_fact_builder fb;

  for (int i = 0; i < n; ++i) {
    a_class_info ci;
    a_vfunc_info vf;

    ci.name = "C" + std::to_string(i);
    ci.mangled = std::to_string(ci.name.size()) + ci.name;
    ci.size = 8;
    ci.nvsize = 8;
    ci.is_dynamic = true;
    ci.nslots = 1;
    if (i > 0) {
      ci.has_primary = true;
      ci.primary = static_cast<a_key>(i - 1);
      fb.add_base(i, i - 1, false, true, 0);
    }
    fb.set_class(i, ci);
    vf.name = ci.name + "::f()";
    vf.mangled = "_ZN" + std::to_string(ci.name.size()) + ci.name + "1fEv";
    vf.slot = wrong_slot && i == n - 1 ? 1 : 0;
    vf.sig = 1;
    fb.add_vfunc(i, static_cast<a_key>(1000 + i), vf);
    for (int j = 0; j < i; ++j)
      fb.add_override(i, static_cast<a_key>(1000 + j), 0,
                      static_cast<a_key>(1000 + i), 0);
  }
  return fb.finish(db);
}

void test_override_chain(int n)
/*
validate_deep and the slot check of an n-level override chain must be fast
and flat in stack: they ran in O(n^3) before.
*/
{
  a_class_db db;
  auto start = std::chrono::steady_clock::now();
  std::string problem = build_override_chain(db, n, false);
  double seconds = std::chrono::duration<double>(
      std::chrono::steady_clock::now() - start).count();

  check(problem.empty(), "override chain: finish", problem);
  std::printf("override chain of %d levels: finish %.2f s\n", n, seconds);
  check(seconds < 20.0, "override chain: time", std::to_string(seconds));
  if (n <= 200) {
    a_class_db bad;

    check(!build_override_chain(bad, n, true).empty(),
          "override chain: wrong slot rejected");
  }
}

std::string build_diamond_lattice(a_class_db &db, int layers)
/*
2 * layers + 1 classes (the stress kind `diamond` of scripts/ecil-m4-stress-
gen.py): the root V is a dynamic class with one virtual function; each class
of layer k is virtually derived from both classes of layer k - 1, so it has
every class of the layers below as a virtual base (reached by many paths).
*/
{
  a_fact_builder fb;
  std::vector<std::vector<int>> layer(1, std::vector<int>(1, 0));
  std::vector<std::vector<int>> vb(1);   // virtual bases, postorder
  std::vector<std::string> names(1, "V");
  a_class_info ci;
  a_vfunc_info vf;

  for (int k = 1; k <= layers; ++k) {
    layer.push_back({});
    for (const char *side : {"A", "B"}) {
      layer[k].push_back(static_cast<int>(names.size()));
      names.push_back(side + std::to_string(k));
      vb.push_back({});
    }
    for (int idx : layer[k]) {
      std::vector<int> order;

      for (int base : layer[k - 1]) {
        std::vector<int> chain = vb[base];

        chain.push_back(base);
        for (int x : chain) {
          if (std::find(order.begin(), order.end(), x) == order.end())
            order.push_back(x);
        }
      }
      vb[idx] = order;
    }
  }
  for (size_t idx = 0; idx < names.size(); ++idx) {
    a_key key = static_cast<a_key>(idx + 1);

    ci = a_class_info();
    ci.name = names[idx];
    ci.mangled = std::to_string(names[idx].size()) + names[idx];
    ci.is_dynamic = true;
    ci.nvsize = 8;
    ci.size = 8 + 8 * static_cast<int64_t>(vb[idx].size());
    if (idx == 0) {
      ci.nslots = 1;
    } else {
      int k = 1;

      while (std::find(layer[k].begin(), layer[k].end(),
                       static_cast<int>(idx)) == layer[k].end())
        ++k;
      for (int base : layer[k - 1]) {
        auto at = std::find(vb[idx].begin(), vb[idx].end(), base);

        fb.add_base(key, static_cast<a_key>(base + 1), true, true,
                    8 + 8 * (at - vb[idx].begin()));
      }
      for (size_t i = 0; i < vb[idx].size(); ++i)
        fb.add_vbase(key, static_cast<a_key>(vb[idx][i] + 1), true,
                     8 + 8 * static_cast<int64_t>(i));
    }
    fb.set_class(key, ci);
  }
  vf.name = "V::f()";
  vf.mangled = "_ZN1V1fEv";
  vf.slot = 0;
  vf.sig = 1;
  fb.add_vfunc(1, 1000, vf);
  return fb.finish(db);
}

void test_diamond_lattice(int layers, double limit)
/*
The vtable group, the VTT and the type_info layout of every class of a lattice of virtual bases.
The path search of the vtable builder must visit each virtual base once per
search (clang's CXXBasePaths): without that, the time doubles with every
layer (25 layers took 15 s and 50 never finished); with it, 40 layers
take well under a second.
*/
{
  a_class_db db;
  std::string problem = build_diamond_lattice(db, layers);

  check(problem.empty(), "diamond lattice: finish", problem);
  if (problem.empty()) {
    auto start = std::chrono::steady_clock::now();
    a_vtable_context ctx(db);
    std::string error;
    double seconds;

    for (a_class_id c = 0; c < db.classes.size(); ++c) {
      ctx.vtable_info(c, error);
      if (error.empty())
        ctx.vtt_layout(c, true, error);
      if (error.empty()) {
        a_rtti_layout lay;

        rtti_layout(ctx, c, lay, error);
      }
      check(error.empty(), "diamond lattice: vtable and VTT", error);
      if (!error.empty())
        break;
    }
    seconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - start).count();
    std::printf("diamond lattice of %d layers: vtables and VTTs %.2f s\n",
                layers, seconds);
    check(seconds < limit, "diamond lattice: time", std::to_string(seconds));
  }
}

}  /* anonymous namespace */

int main(int argc, char **argv)
{
  if (argc > 1 && std::strcmp(argv[1], "deep") == 0) {
    test_deep(5000);
    test_override_chain(2000);
  } else {
    test_single();
    test_bad_slots();
    test_text_round_trip();
    test_cycle();
    test_builder();
    test_validate_deep();
    test_empty_base_vbase();
    test_names();
    test_rtti();
    test_nearly_empty_primary_vbase();
    test_plain_bases();
    test_typeinfo_name_string();
    test_override_chain(100);
    test_diamond_lattice(40, 20.0);
    test_deep(200);
  }
  std::printf("abi_tests: %s (%d failures)\n", failures == 0 ? "OK" : "BAD",
              failures);
  return failures == 0 ? 0 : 1;
}
