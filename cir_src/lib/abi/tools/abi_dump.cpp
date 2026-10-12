/*
Part of the EDG Compiler Project, under the Apache License v2.0 with LLVM
Exceptions.
See https://edgcpp.org/LICENSE.txt for license information.
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
*/

/*
abi_dump -- print the vtable layouts, construction vtables, VTTs and slot
checks of a class-facts file (abi_text.h) in clang's -fdump-vtable-layouts
format.

  abi_dump [--check] [--vtt] [--symbols] [--rtti] FACTS.txt [CLASS-NAME ...]

Every dynamic class (or the named ones) gets its vtable group and, for classes
with virtual bases, its construction vtables (and with --vtt its VTT).  --check
adds the slot checks and prints their findings on standard error (exit status
1 if there are any).

--symbols (needs `mangled` on the classes and functions of the facts) prints
instead the special symbols of every dynamic class, one per line:
  vtable SYM / vtt SYM / ctor-vtable SYM [inexact] / thunk SYM CLASS /
thunk-opt SYM CLASS (the thunks of the entries of a vtable, which its
emission needs; the thunks recorded for a method, which clang emits with the
method's definition).  SYM is "?" for a thunk that cannot be named (a
function without a symbol).  --rtti prints the type_info layout of every class:
  rtti CLASS (kind), then print_rtti's field lines, blank line.
*/

#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

#include "abi_names.h"
#include "abi_print.h"
#include "abi_rtti.h"
#include "abi_text.h"

using namespace edg_abi;

namespace {

std::string thunk_target(const a_class_db &db, const a_vtable_layout &layout,
                         uint64_t index)
/*
The symbol of the function a thunk at component `index` calls ("?" if the
facts do not name it).
*/
{
  const a_component &comp = layout.components[index];
  const a_fn_fact &fn = db.fn(comp.id);
  std::string sym = fn.mangled;

  if (comp.kind == ck_complete_dtor)
    sym = dtor_variant_symbol(sym, '1');
  else if (comp.kind == ck_deleting_dtor)
    sym = dtor_variant_symbol(sym, '0');
  return sym.empty() ? "?" : sym;
}  /* thunk_target */

void print_thunks(const a_class_db &db, const a_vtable_layout &layout,
                  const std::string &owner)
/*
The thunk symbols of one vtable group.
*/
{
  for (const auto &t : layout.thunks) {
    std::string target = thunk_target(db, layout, t.first);
    std::string sym = target == "?" ? "?" :
        thunk_symbol(t.second.this_adj, t.second.return_adj, target);

    std::printf("thunk %s %s\n", sym.empty() ? "?" : sym.c_str(),
                owner.c_str());
  }
}  /* print_thunks */

void print_map_thunks(const a_class_db &db, const a_vtable_info &info,
                      const std::string &owner)
/*
The thunks clang emits along with a method's definition
(CodeGenVTables::EmitThunks: everything its vtable groups recorded for the
method; a destructor: the complete and the deleting one).  They are needed
only if the method is defined in the translation unit.
*/
{
  for (const auto &entry : info.thunks) {
    const a_fn_fact &fn = db.fn(entry.first);

    for (const a_thunk_info &t : entry.second) {
      const char *variants = fn.special == fs_destructor ? "10" : "x";

      for (const char *v = variants; *v != '\0'; ++v) {
        std::string target = *v == 'x' ? fn.mangled :
            dtor_variant_symbol(fn.mangled, *v);
        std::string sym = target.empty() ? "" :
            thunk_symbol(t.this_adj, t.return_adj, target);

        std::printf("thunk-opt %s %s\n", sym.empty() ? "?" : sym.c_str(),
                    owner.c_str());
      }
    }
  }
}  /* print_map_thunks */

int symbols(a_class_db &db, a_vtable_context &ctx,
            const std::vector<std::string> &names)
/*
--symbols.
*/
{
  int status = 0;

  for (a_class_id c = 0; c < db.classes.size(); ++c) {
    const a_class_fact &cf = db.classes[c];
    bool wanted = names.empty();
    std::string error;

    for (const std::string &n : names)
      wanted = wanted || n == cf.name;
    if (!cf.is_dynamic || !wanted)
      continue;
    const a_vtable_info &info = ctx.vtable_info(c, error);

    if (!error.empty()) {
      std::fprintf(stderr, "abi_dump: %s: %s\n", cf.name.c_str(),
                   error.c_str());
      status = 1;
      continue;
    }
    print_map_thunks(db, info, cf.name);
    if (cf.mangled.empty())
      continue;
    std::printf("vtable %s %s\n", vtable_symbol(cf.mangled).c_str(),
                cf.name.c_str());
    print_thunks(db, info.layout, cf.name);
    if (!cf.vbases.empty()) {
      std::unique_ptr<a_vtt_layout> layout = ctx.vtt_layout(c, true, error);

      if (!layout) {
        std::fprintf(stderr, "abi_dump: %s: VTT: %s\n", cf.name.c_str(),
                     error.c_str());
        status = 1;
        continue;
      }
      std::printf("vtt %s %s\n", vtt_symbol(cf.mangled).c_str(),
                  cf.name.c_str());
      for (size_t k = 1; k < layout->vtables.size(); ++k) {
        const a_vtt_vtable &vt = layout->vtables[k];
        const a_class_fact &bf = db.cls(vt.base.cls);
        std::unique_ptr<a_vtable_layout> cv = ctx.construction_vtable_layout(
            vt.base.cls, vt.base.offset, vt.base_is_virtual, c, error);
        bool exact = true;

        if (!cv) {
          std::fprintf(stderr, "abi_dump: %s: construction vtable: %s\n",
                       cf.name.c_str(), error.c_str());
          status = 1;
          continue;
        }
        if (!bf.mangled.empty()) {
          std::printf("ctor-vtable %s%s %s\n",
                      ctor_vtable_symbol(cf.mangled, vt.base.offset,
                                         bf.mangled, exact).c_str(),
                      exact ? "" : " inexact", cf.name.c_str());
        }
        print_thunks(db, *cv, cf.name);
      }
    }
  }
  return status;
}  /* symbols */

int rtti(a_class_db &db, a_vtable_context &ctx,
         const std::vector<std::string> &names)
/*
--rtti.
*/
{
  int status = 0;
  static const char *kinds[] = {"class", "si", "vmi"};

  for (a_class_id c = 0; c < db.classes.size(); ++c) {
    const a_class_fact &cf = db.classes[c];
    bool wanted = names.empty();
    std::string error;
    a_rtti_layout layout;

    for (const std::string &n : names)
      wanted = wanted || n == cf.name;
    if (!wanted)
      continue;
    if (!rtti_layout(ctx, c, layout, error)) {
      std::fprintf(stderr, "abi_dump: %s: rtti: %s\n", cf.name.c_str(),
                   error.c_str());
      status = 1;
      continue;
    }
    std::printf("rtti %s %s%s\n", cf.name.c_str(), kinds[layout.kind],
                cf.is_dynamic ? " dynamic" : "");
    std::fputs(print_rtti(db, layout).c_str(), stdout);
    std::printf("\n");
  }
  return status;
}  /* rtti */

}  /* anonymous namespace */

int main(int argc, char **argv)
{
  bool want_symbols = false;
  bool want_rtti = false;
  bool check = false;
  bool vtt = false;
  const char *path = nullptr;
  std::vector<std::string> names;
  int status = 0;

  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--check") == 0)
      check = true;
    else if (std::strcmp(argv[i], "--vtt") == 0)
      vtt = true;
    else if (std::strcmp(argv[i], "--symbols") == 0)
      want_symbols = true;
    else if (std::strcmp(argv[i], "--rtti") == 0)
      want_rtti = true;
    else if (path == nullptr)
      path = argv[i];
    else
      names.push_back(argv[i]);
  }
  if (path == nullptr) {
    std::fprintf(stderr, "usage: abi_dump [--check] [--vtt] FACTS.txt "
                         "[CLASS ...]\n");
    return 2;
  }
  std::ifstream file(path);
  std::stringstream buffer;
  a_class_db db;

  buffer << file.rdbuf();
  std::string problem = parse_facts(buffer.str(), db);

  if (!problem.empty()) {
    std::fprintf(stderr, "abi_dump: %s\n", problem.c_str());
    return 2;
  }
  a_vtable_context ctx(db);

  if (check) {
    /* The finish-time checks of the class facts (the fact builder runs
       them): the consistency of the facts and the slots of the functions
       against the layout. */
    std::string deep = db.validate_deep();

    if (deep.empty())
      deep = check_all_slots(db);
    if (!deep.empty()) {
      std::fprintf(stderr, "abi_dump: facts: %s\n", deep.c_str());
      status = 1;
    }
  }
  if (want_symbols || want_rtti) {
    if (want_symbols)
      status = symbols(db, ctx, names);
    if (want_rtti && rtti(db, ctx, names) != 0)
      status = 1;
    return status;
  }
  for (a_class_id c = 0; c < db.classes.size(); ++c) {
    const a_class_fact &cf = db.classes[c];
    bool wanted = names.empty();
    std::string error;

    for (const std::string &n : names)
      wanted = wanted || n == cf.name;
    if (!cf.is_dynamic || !wanted)
      continue;
    std::string text = print_vtable(ctx, c, error);

    if (!error.empty()) {
      std::fprintf(stderr, "abi_dump: %s: %s\n", cf.name.c_str(),
                   error.c_str());
      status = 1;
      continue;
    }
    std::fputs(text.c_str(), stdout);
    if (!cf.vbases.empty()) {
      std::unique_ptr<a_vtt_layout> layout = ctx.vtt_layout(c, true, error);

      if (!layout) {
        std::fprintf(stderr, "abi_dump: %s: VTT: %s\n", cf.name.c_str(),
                     error.c_str());
        status = 1;
        continue;
      }
      for (size_t k = 1; k < layout->vtables.size(); ++k) {
        const a_vtt_vtable &vt = layout->vtables[k];
        std::unique_ptr<a_vtable_layout> cv = ctx.construction_vtable_layout(
            vt.base.cls, vt.base.offset, vt.base_is_virtual, c, error);

        if (!cv) {
          std::fprintf(stderr, "abi_dump: %s: construction vtable: %s\n",
                       cf.name.c_str(), error.c_str());
          status = 1;
          continue;
        }
        std::fputs(print_construction_vtable(db, *cv, vt.base.cls,
                                             vt.base.offset, c).c_str(),
                   stdout);
      }
      if (vtt)
        std::fputs(print_vtt(db, *layout, c).c_str(), stdout);
    }
    if (check) {
      std::string slot = check_slots(ctx, c);
      std::string builder = check_slots_against_builder(ctx, c);

      if (!slot.empty() || !builder.empty()) {
        std::fprintf(stderr, "abi_dump: %s: %s%s%s\n", cf.name.c_str(),
                     slot.c_str(), slot.empty() || builder.empty() ? "" : "; ",
                     builder.c_str());
        status = 1;
      }
    }
  }
  return status;
}
