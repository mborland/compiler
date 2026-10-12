/*
Part of the EDG Compiler Project, under the Apache License v2.0 with LLVM
Exceptions.
See https://edgcpp.org/LICENSE.txt for license information.
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
*/

/*
ecil_classes.cpp -- the validator rules of the ECIL 1.2 records that need the
id tables (spec section 12): the class facts (TY_VPTR, VIRTUAL_BASE fields,
CLASS_MANGLE/INFO/BASES/VFUNCS/OVERRIDES and the first-need order), the ABI
data and structor records, the exception and member-pointer records.  The
structural rules (brackets, prologue order) are the bracket tracker's;
value-stack rules and the semantic checks of the class facts (validate_deep)
are the consumer's.  Heap data only; nothing recurses.
*/

#include <algorithm>

#include "ecil_reader.h"

namespace ecil {

namespace {

std::string number(uint64_t v)
/*
Decimal text of v.
*/
{
  return std::to_string(static_cast<unsigned long long>(v));
}  /* number */

bool operand_id(const an_operand &op, uint64_t &value)
/*
True with the id if op is a present (non-nil, non-zero) id.
*/
{
  return operand_u64(op, value) && value != 0;
}  /* operand_id */

bool is_power_of_two(uint64_t v)
/*
True for 1, 2, 4, ...
*/
{
  return v != 0 && (v & (v - 1)) == 0;
}  /* is_power_of_two */

const uint8_t all_class_stages = cs_mangle | cs_info | cs_bases | cs_vfuncs |
                                 cs_overrides;
const uint8_t layout_stages = cs_mangle | cs_info | cs_bases;

}  /* anonymous namespace */

bool a_ref_checker::finish_record_facts(std::string &problem)
/*
The last REC_FIELD of the current record was read: derive the structural
facts (vptr, virtual bases, dynamic-ness) the later records are checked
against.
*/
{
  bool ok = true;
  a_class_facts *f = ids_.class_facts(cur_record_);

  f->complete = true;
  for (const a_class_field &field : f->fields) {
    const a_gid_entry *te = ids_.find_gid(field.type);

    if (te != nullptr && te->kind == gk_vptr)
      f->has_vptr = true;
    if (field.flags & ECIL_FIELD_VIRTUAL_BASE) {
      f->vbases.push_back(field.type);
    } else if (field.flags & ECIL_FIELD_BASE) {
      const a_class_facts *b = ids_.class_facts(field.type);

      f->has_base_fields = true;
      if (b != nullptr && b->dynamic)
        f->dynamic = true;
    }
  }
  std::sort(f->vbases.begin(), f->vbases.end());
  if (std::adjacent_find(f->vbases.begin(), f->vbases.end()) !=
      f->vbases.end()) {
    problem = "TY_RECORD_BODY of G-id " + number(cur_record_) + " has two "
              "VIRTUAL_BASE fields of one class";
    ok = false;
  }
  if (f->has_vptr || !f->vbases.empty())
    f->dynamic = true;
  /* A dynamic class (an own vptr, a virtual base, or a base that is
     dynamic) has its layout facts right away (12.2): the bases' bodies
     came first, so the consumer knows it here. */
  if (ok && f->dynamic) {
    layout_class_ = cur_record_;
    layout_expect_ = 1;
  }
  return ok;
}  /* a_ref_checker::finish_record_facts */

uint64_t a_ref_checker::nvsize_estimate(const a_class_facts &f) const
/*
The non-virtual size of a base class as far as it is known: its CLASS_INFO
nvsize, else its data size (12.2: a class is laid out with nvsize = data_size
until its CLASS_INFO; a class with a vptr or virtual bases has it by then).
*/
{
  return (f.stage & cs_info) ? f.nvsize : f.data_size;
}  /* a_ref_checker::nvsize_estimate */

bool a_ref_checker::class_of_function(const a_gid_entry *fn,
                                      uint32_t &cls) const
/*
The class a member function's first parameter (`this`) points to.
*/
{
  bool found = false;

  if (fn != nullptr && fn->kind == gk_func) {
    const a_gid_entry *ft = ids_.find_gid(fn->element);

    if (ft != nullptr && ft->kind == gk_func_type && ft->extent >= 1) {
      const a_gid_entry *pt = ids_.find_gid(ft->size);

      if (pt != nullptr && pt->kind == gk_ptr) {
        const a_gid_entry *rec = ids_.find_gid(pt->element);

        if (rec != nullptr && rec->kind == gk_record) {
          cls = pt->element;
          found = true;
        }
      }
    }
  }
  return found;
}  /* a_ref_checker::class_of_function */

bool a_ref_checker::need_class_facts(uint64_t g, bool need_mangle,
                                     const char *what, std::string &problem)
/*
First need (12.2): the facts of the class come before the record that needs
them, as far as the structure of the class asks for them: a dynamic class
needs all five records, a class with bases or a CLASS_INFO needs CLASS_INFO
and CLASS_BASES, and CLASS_MANGLE when the names are needed.
*/
{
  bool ok = true;
  const a_gid_entry *e = ids_.find_gid(g);
  const a_class_facts *f = ids_.class_facts(static_cast<uint32_t>(g));
  uint8_t want = 0;

  if (e != nullptr && e->kind == gk_record) {
    if (f != nullptr && f->complete) {
      if (f->dynamic)
        want = all_class_stages;
      else if (f->has_base_fields || (f->stage & cs_info))
        want = cs_mangle | cs_info | cs_bases;
    }
    if (need_mangle)
      want |= cs_mangle;
    if ((f == nullptr ? 0 : f->stage & want) != want) {
      problem = std::string("the facts of class G-id ") + number(g) +
                " (CLASS_MANGLE, CLASS_INFO, CLASS_BASES, CLASS_VFUNCS, "
                "CLASS_OVERRIDES, as its structure needs them) must come "
                "before " + what;
      ok = false;
    }
  }
  return ok;
}  /* a_ref_checker::need_class_facts */

bool a_ref_checker::check_class_info(const a_record &r, std::string &problem)
/*
CLASS_INFO(t, nvsize, nvalign, primary_idx|nil, nslots).
*/
{
  bool ok = true;
  uint64_t t = 0;
  uint64_t nvsize = 0;
  uint64_t nvalign = 0;
  uint64_t primary = 0;
  uint64_t nslots = 0;
  const a_gid_entry *e = nullptr;
  a_class_facts *f = nullptr;
  bool has_primary = r.ops[3].kind != an_operand_kind::nil;

  ok = need_type(r.ops[0], false, problem);
  if (ok && operand_id(r.ops[0], t)) {
    e = ids_.find_gid(t);
    f = ids_.class_facts(static_cast<uint32_t>(t));
  }
  operand_u64(r.ops[1], nvsize);
  operand_u64(r.ops[2], nvalign);
  operand_u64(r.ops[4], nslots);
  if (!ok) {
    /* The type problem is set. */
  } else if (e == nullptr || e->kind != gk_record || f == nullptr ||
             !f->complete) {
    problem = "CLASS_INFO names G-id " + number(t) + ", which is not a "
              "record with a body";
    ok = false;
  } else if (!(f->stage & cs_mangle)) {
    problem = "CLASS_INFO of G-id " + number(t) + " needs its CLASS_MANGLE "
              "first";
    ok = false;
  } else if (f->stage & cs_info) {
    problem = "CLASS_INFO of G-id " + number(t) + " appears twice";
    ok = false;
  } else if (nvsize > f->size || nvsize < f->non_vbase_end) {
    problem = "CLASS_INFO nvsize " + number(nvsize) + " of G-id " +
              number(t) + " is above the size " + number(f->size) +
              " or below the end of its last field that is not a virtual "
              "base (" + number(f->non_vbase_end) + ")";
    ok = false;
  } else if (f->vbases.empty() && nvsize < f->data_size) {
    problem = "CLASS_INFO nvsize " + number(nvsize) + " of G-id " +
              number(t) + " is below its data_size " +
              number(f->data_size) + " (no virtual bases)";
    ok = false;
  } else if (!is_power_of_two(nvalign) || nvalign > f->align) {
    problem = "CLASS_INFO nvalign " + number(nvalign) + " of G-id " +
              number(t) + " is not a power of two within the alignment " +
              number(f->align);
    ok = false;
  } else if (has_primary) {
    operand_u64(r.ops[3], primary);
    if (!f->dynamic) {
      problem = "CLASS_INFO primary_idx of G-id " + number(t) + ", a class "
                "without a vptr";
      ok = false;
    } else if (primary >= f->fields.size() ||
               !(f->fields[primary].flags & (ECIL_FIELD_BASE |
                                             ECIL_FIELD_VIRTUAL_BASE)) ||
               f->fields[primary].offset != 0) {
      problem = "CLASS_INFO primary_idx of G-id " + number(t) + " is not a "
                "BASE or VIRTUAL_BASE field at offset 0";
      ok = false;
    } else {
      const a_class_facts *p = ids_.class_facts(f->fields[primary].type);

      if (p == nullptr || !p->dynamic) {
        problem = "CLASS_INFO primary_idx of G-id " + number(t) +
                  " names a base that is not a dynamic class";
        ok = false;
      } else if (!(p->stage & cs_info)) {
        problem = "the CLASS_INFO of the primary base of G-id " + number(t) +
                  " must come first";
        ok = false;
      } else if (nslots < p->nslots) {
        problem = "CLASS_INFO nslots " + number(nslots) + " of G-id " +
                  number(t) + " is below that of its primary base (" +
                  number(p->nslots) + ")";
        ok = false;
      }
    }
  }
  if (ok && f->dynamic && f->has_vptr == has_primary) {
    problem = std::string("CLASS_INFO of G-id ") + number(t) + ": a dynamic "
              "class has either its own TY_VPTR field or a primary_idx, " +
              (has_primary ? "never both" : "never neither");
    ok = false;
  }
  if (ok && !f->dynamic && nslots != 0) {
    problem = "CLASS_INFO nslots of G-id " + number(t) + ", a class without "
              "a vptr";
    ok = false;
  }
  if (ok) {
    f->nvsize = nvsize;
    f->nslots = nslots;
    f->stage |= cs_info;
  }
  return ok;
}  /* a_ref_checker::check_class_info */

bool a_ref_checker::check_class_bases(const a_record &r, std::string &problem)
/*
CLASS_BASES(t, [(field_idx, flags)...]).
*/
{
  bool ok = need_type(r.ops[0], false, problem);
  uint64_t t = 0;
  a_class_facts *f = nullptr;

  if (ok && operand_id(r.ops[0], t))
    f = ids_.class_facts(static_cast<uint32_t>(t));
  if (!ok) {
    /* The type problem is set. */
  } else if (f == nullptr || !(f->stage & cs_info) || (f->stage & cs_bases)) {
    problem = "CLASS_BASES of G-id " + number(t) + " must follow its "
              "CLASS_INFO, once";
    ok = false;
  } else if (r.ops[1].count % 2 != 0) {
    problem = "CLASS_BASES array length is not a multiple of 2";
    ok = false;
  } else {
    an_array_reader reader = array_elements(r.ops[1]);
    std::vector<bool> used(f->fields.size(), false);
    std::vector<uint32_t> expected;
    std::vector<uint32_t> direct;
    uint64_t last_base = 0;
    bool have_base = false;
    size_t base_entries = 0;
    size_t base_fields = 0;
    uint64_t idx = 0;
    uint64_t flags = 0;

    for (const a_class_field &field : f->fields) {
      if (field.flags & ECIL_FIELD_BASE)
        ++base_fields;
    }
    while (ok && next_uint_element(reader, idx) &&
           next_uint_element(reader, flags)) {
      if (idx >= f->fields.size() ||
          !(f->fields[idx].flags & (ECIL_FIELD_BASE |
                                    ECIL_FIELD_VIRTUAL_BASE))) {
        problem = "CLASS_BASES entry " + number(idx) + " is not a BASE or "
                  "VIRTUAL_BASE field of G-id " + number(t);
        ok = false;
      } else if (flags & ~static_cast<uint64_t>(ECIL_CLASS_BASE_PUBLIC)) {
        problem = "CLASS_BASES: unknown flag bits";
        ok = false;
      } else if (used[idx]) {
        problem = "CLASS_BASES names field " + number(idx) + " twice";
        ok = false;
      } else {
        const a_class_field &field = f->fields[idx];
        const a_class_facts *b = ids_.class_facts(field.type);

        used[idx] = true;
        direct.push_back(field.type);
        if (field.flags & ECIL_FIELD_BASE) {
          if (have_base && idx < last_base) {
            problem = "CLASS_BASES: the BASE entries are in increasing "
                      "field order";
            ok = false;
          }
          have_base = true;
          last_base = idx;
          ++base_entries;
        } else {
          expected.push_back(field.type);
        }
        if (ok && b != nullptr && b->complete &&
            b->dynamic && (b->stage & layout_stages) != layout_stages) {
          /* Bases first (12.2): a dynamic base had its layout facts right
             away.  The other facts of a base
             (the vtable facts, the facts of any other base) are needed
             only by the records that use them. */
          problem = "the layout facts (CLASS_MANGLE, CLASS_INFO, "
                    "CLASS_BASES) of class G-id " + number(field.type) +
                    ", a dynamic class, must come before CLASS_BASES of a class "
                    "derived from it";
          ok = false;
        }
        if (ok && b != nullptr)
          expected.insert(expected.end(), b->vbases.begin(),
                          b->vbases.end());
      }
    }
    if (ok && base_entries != base_fields) {
      problem = "CLASS_BASES of G-id " + number(t) + " omits a BASE field";
      ok = false;
    }
    if (ok) {
      std::sort(expected.begin(), expected.end());
      expected.erase(std::unique(expected.begin(), expected.end()),
                     expected.end());
      if (expected != f->vbases) {
        problem = "the VIRTUAL_BASE fields of G-id " + number(t) + " are not "
                  "the direct virtual bases plus the virtual bases of the "
                  "direct bases";
        ok = false;
      }
    }
    if (ok) {
      f->direct_bases = direct;
      f->stage |= cs_bases;
    }
  }
  return ok;
}  /* a_ref_checker::check_class_bases */

bool a_ref_checker::check_class_vfuncs(const a_record &r, std::string &problem)
/*
CLASS_VFUNCS(t, [(g, slot, flags, sig)...]).
*/
{
  bool ok = need_type(r.ops[0], false, problem);
  uint64_t t = 0;
  a_class_facts *f = nullptr;

  if (ok && operand_id(r.ops[0], t))
    f = ids_.class_facts(static_cast<uint32_t>(t));
  if (!ok) {
    /* The type problem is set. */
  } else if (f == nullptr || !(f->stage & cs_bases) ||
             (f->stage & cs_vfuncs)) {
    problem = "CLASS_VFUNCS of G-id " + number(t) + " must follow its "
              "CLASS_BASES, once";
    ok = false;
  } else if (!f->dynamic) {
    problem = "CLASS_VFUNCS of G-id " + number(t) + ", a class without a "
              "vptr";
    ok = false;
  } else if (r.ops[1].count % 4 != 0) {
    problem = "CLASS_VFUNCS array length is not a multiple of 4";
    ok = false;
  } else {
    an_array_reader reader = array_elements(r.ops[1]);
    uint64_t g = 0;
    uint64_t slot = 0;
    uint64_t flags = 0;
    uint64_t sig = 0;

    while (ok && next_uint_element(reader, g) &&
           next_uint_element(reader, slot) &&
           next_uint_element(reader, flags) &&
           next_uint_element(reader, sig)) {
      a_gid_entry *fn = ids_.find_gid(g);
      uint32_t owner = 0;
      bool is_dtor = false;

      if (fn == nullptr || fn->kind != gk_func || !class_of_function(fn, owner)
          || owner != t) {
        problem = "CLASS_VFUNCS entry G-id " + number(g) + " is not a "
                  "declared member function of G-id " + number(t);
        ok = false;
      } else if (fn->extent != 0) {
        problem = "virtual function G-id " + number(g) + " is listed by two "
                  "CLASS_VFUNCS entries";
        ok = false;
      } else if (flags & ~static_cast<uint64_t>(ECIL_VFUNC_PURE |
                                                ECIL_VFUNC_DELETED |
                                                ECIL_VFUNC_IMPLICIT)) {
        problem = "CLASS_VFUNCS: unknown flag bits";
        ok = false;
      } else {
        is_dtor = (fn->role & sr_dtor) != 0;
        if (is_dtor && !(fn->role & sr_complete)) {
          problem = "CLASS_VFUNCS lists a destructor as its D1, the complete "
                    "variant (G-id " + number(g) + " is not)";
          ok = false;
        } else if ((sig == 0) != is_dtor) {
          problem = "CLASS_VFUNCS sig is 0 exactly for destructors (G-id " +
                    number(g) + ")";
          ok = false;
        } else if (sig > 0xFFFFFFFFull || slot + (is_dtor ? 2 : 1) >
                                              f->nslots) {
          problem = "CLASS_VFUNCS slot " + number(slot) + " of G-id " +
                    number(g) + " is outside the " + number(f->nslots) +
                    " slots of the class";
          ok = false;
        } else {
          fn->extent = t;
        }
      }
    }
    /* Bases first (12.2): the vtable facts of each dynamic base come
       before those of the class. */
    for (size_t i = 0; ok && i < f->fields.size(); ++i) {
      const a_class_field &field = f->fields[i];
      const a_class_facts *b = ids_.class_facts(field.type);

      if ((field.flags & (ECIL_FIELD_BASE | ECIL_FIELD_VIRTUAL_BASE)) &&
          b != nullptr && b->complete && b->dynamic &&
          (b->stage & all_class_stages) != all_class_stages) {
        problem = "the vtable facts (CLASS_VFUNCS, CLASS_OVERRIDES) of the "
                  "dynamic base G-id " + number(field.type) + " must come "
                  "before CLASS_VFUNCS of a class derived from it";
        ok = false;
      }
    }
    if (ok)
      f->stage |= cs_vfuncs;
  }
  return ok;
}  /* a_ref_checker::check_class_vfuncs */

bool a_ref_checker::check_class_overrides(const a_record &r,
                                          std::string &problem)
/*
CLASS_OVERRIDES(t, [(g_fn, sub_off, g_ovr, ovr_off)...]).
*/
{
  bool ok = need_type(r.ops[0], false, problem);
  uint64_t t = 0;
  a_class_facts *f = nullptr;

  if (ok && operand_id(r.ops[0], t))
    f = ids_.class_facts(static_cast<uint32_t>(t));
  if (!ok) {
    /* The type problem is set. */
  } else if (f == nullptr || !(f->stage & cs_vfuncs) ||
             (f->stage & cs_overrides) || !f->dynamic) {
    problem = "CLASS_OVERRIDES of G-id " + number(t) + " must follow its "
              "CLASS_VFUNCS, once, and is for a dynamic class";
    ok = false;
  } else if (r.ops[1].count % 4 != 0) {
    problem = "CLASS_OVERRIDES array length is not a multiple of 4";
    ok = false;
  } else {
    an_array_reader reader = array_elements(r.ops[1]);
    std::vector<std::pair<uint64_t, uint64_t>> keys;
    uint64_t fn = 0;
    uint64_t sub = 0;
    uint64_t ovr = 0;
    uint64_t ovr_off = 0;

    while (ok && next_uint_element(reader, fn) &&
           next_uint_element(reader, sub) && next_uint_element(reader, ovr) &&
           next_uint_element(reader, ovr_off)) {
      const a_gid_entry *a = ids_.find_gid(fn);
      const a_gid_entry *b = ids_.find_gid(ovr);

      if (a == nullptr || a->kind != gk_func || a->extent == 0 ||
          b == nullptr || b->kind != gk_func || b->extent == 0) {
        problem = "CLASS_OVERRIDES names a function that no CLASS_VFUNCS "
                  "lists (G-ids " + number(fn) + ", " + number(ovr) + ")";
        ok = false;
      } else if (fn == ovr) {
        problem = "CLASS_OVERRIDES lists only entries with g_ovr != g_fn "
                  "(G-id " + number(fn) + ")";
        ok = false;
      } else if (sub > f->size || ovr_off > f->size) {
        problem = "CLASS_OVERRIDES offset beyond the size of G-id " +
                  number(t);
        ok = false;
      } else {
        keys.push_back(std::make_pair(fn, sub));
      }
    }
    if (ok) {
      std::sort(keys.begin(), keys.end());
      if (std::adjacent_find(keys.begin(), keys.end()) != keys.end()) {
        problem = "CLASS_OVERRIDES has two final overriders for one "
                  "function at one subobject";
        ok = false;
      }
    }
    if (ok)
      f->stage |= cs_overrides;
  }
  return ok;
}  /* a_ref_checker::check_class_overrides */

const a_gid_entry *a_ref_checker::typeinfo_entry(const an_operand &op,
                                                 bool class_kind,
                                                 bool optional,
                                                 std::string &problem)
/*
An operand naming a DECL_ABI_DATA typeinfo (of a class if class_kind).
*/
{
  uint64_t g = 0;
  const a_gid_entry *e = nullptr;

  if (operand_id(op, g)) {
    e = ids_.find_gid(g);
    if (e == nullptr || e->kind != gk_abi_data || e->role != eadk_typeinfo) {
      problem = "G-id " + number(g) + " is not a DECL_ABI_DATA typeinfo";
      e = nullptr;
    } else if (class_kind) {
      const a_gid_entry *t = ids_.find_gid(e->element);

      if (t == nullptr || t->kind != gk_record) {
        problem = "G-id " + number(g) + " is not the typeinfo of a class";
        e = nullptr;
      }
    }
  } else if (!optional) {
    problem = "missing typeinfo G-id";
  }
  return e;
}  /* a_ref_checker::typeinfo_entry */

bool a_ref_checker::check_typeinfo_def(const a_record &r,
                                       std::string &problem)
/*
TYPEINFO_DEF(g_ti, kind, flags, g_a|nil, g_b|nil): the content of the
typeinfo of a type that is not a class.
*/
{
  bool ok = true;
  uint64_t g = 0;
  uint64_t def_kind = 0;
  uint64_t def_flags = 0;
  const a_gid_entry *e = nullptr;
  const a_gid_entry *te = nullptr;

  operand_u64(r.ops[1], def_kind);
  operand_u64(r.ops[2], def_flags);
  e = operand_id(r.ops[0], g) ? ids_.find_gid(g) : nullptr;
  te = e != nullptr ? ids_.find_gid(e->element) : nullptr;
  if (e == nullptr || e->kind != gk_abi_data || e->role != eadk_typeinfo ||
      te == nullptr || te->kind == gk_record) {
    problem = "TYPEINFO_DEF names G-id " + number(g) + ", which is not a "
              "non-class DECL_ABI_DATA typeinfo";
    ok = false;
  } else if (e->flags & gf_defined) {
    problem = "TYPEINFO_DEF of G-id " + number(g) + " appears twice";
    ok = false;
  } else if (def_kind != etik_pointer && def_kind != etik_member_pointer &&
             def_flags != 0) {
    problem = "TYPEINFO_DEF flags are for pointer and member_pointer only";
    ok = false;
  } else if (def_kind == etik_fundamental &&
             (e->linkage != elk_external || (e->decl_flags & 1))) {
    problem = "a fundamental typeinfo is an external declaration, never "
              "defined here";
    ok = false;
  } else if (def_kind == etik_pointer || def_kind == etik_member_pointer) {
    ok = typeinfo_entry(r.ops[3], false, false, problem) != nullptr;
    if (ok && def_kind == etik_member_pointer) {
      ok = typeinfo_entry(r.ops[4], true, false, problem) != nullptr;
    } else if (ok && r.ops[4].kind != an_operand_kind::nil) {
      problem = "TYPEINFO_DEF pointer has no class typeinfo (g_b is nil)";
      ok = false;
    }
  } else if (r.ops[3].kind != an_operand_kind::nil ||
             r.ops[4].kind != an_operand_kind::nil) {
    problem = "TYPEINFO_DEF of this kind has no g_a or g_b";
    ok = false;
  }
  if (ok)
    ids_.find_gid(g)->flags |= gf_defined;
  return ok;
}  /* a_ref_checker::check_typeinfo_def */

bool a_ref_checker::check_abi_data(const a_record &r, std::string &problem)
/*
DECL_ABI_DATA(g, kind, t, linkage, flags, sid|nil).
*/
{
  bool ok = true;
  uint64_t g = 0;
  uint64_t kind = 0;
  uint64_t t = 0;
  uint64_t linkage = 0;
  uint64_t flags = 0;
  uint64_t name = 0;
  const a_gid_entry *te = nullptr;
  a_gid_entry *e = nullptr;
  bool is_class = false;

  operand_u64(r.ops[1], kind);
  operand_u64(r.ops[3], linkage);
  operand_u64(r.ops[4], flags);
  operand_id(r.ops[5], name);
  ok = operand_id(r.ops[0], g) && operand_id(r.ops[2], t);
  if (!ok) {
    problem = "DECL_ABI_DATA without a G-id or a type";
  } else {
    ok = need_type(r.ops[2], false, problem) &&
         need_sid(r.ops[5], true, "the name", problem);
  }
  if (ok) {
    te = ids_.find_gid(t);
    is_class = te->kind == gk_record;
    if (kind != eadk_typeinfo && !is_class) {
      problem = "a vtable or VTT is of a class";
      ok = false;
    } else if (kind == eadk_typeinfo && !is_class && name == 0) {
      problem = "the typeinfo of a type that is not a class needs its name "
                "(sid)";
      ok = false;
    } else if (is_class) {
      const a_class_facts *f = ids_.class_facts(static_cast<uint32_t>(t));

      if (f != nullptr && f->complete) {
        ok = need_class_facts(t, true, "DECL_ABI_DATA of the class", problem);
        if (ok && kind == eadk_vtable && !f->dynamic) {
          problem = "a vtable of G-id " + number(t) + ", which is not a "
                    "dynamic class";
          ok = false;
        } else if (ok && kind == eadk_vtt && f->vbases.empty()) {
          problem = "a VTT of G-id " + number(t) + ", which has no virtual "
                    "bases";
          ok = false;
        }
      } else if (kind != eadk_typeinfo) {
        problem = "a vtable or VTT of an incomplete class";
        ok = false;
      } else if (f == nullptr || !(f->stage & cs_mangle)) {
        problem = "the typeinfo of class G-id " + number(t) + " needs its "
                  "CLASS_MANGLE first";
        ok = false;
      }
    }
  }
  if (ok) {
    bool vague = linkage == elk_linkonce_odr || linkage == elk_weak_odr;
    bool defined = (flags & ECIL_ABI_DATA_DEFINED_HERE) != 0;
    bool comdat = (flags & ECIL_ABI_DATA_COMDAT) != 0;

    bool tu_local = (flags & ECIL_ABI_DATA_TU_LOCAL) != 0;

    if (tu_local && kind != eadk_typeinfo) {
      problem = "DECL_ABI_DATA flag TU_LOCAL is for a typeinfo only";
      ok = false;
    } else if (tu_local && linkage != elk_internal) {
      problem = "DECL_ABI_DATA flag TU_LOCAL implies internal linkage";
      ok = false;
    } else if (vague && !defined) {
      problem = "linkonce_odr/weak_odr is only for definitions";
      ok = false;
    } else if (comdat != (defined && vague)) {
      problem = "COMDAT is set if and only if a definition has linkonce_odr "
                "or weak_odr linkage";
      ok = false;
    }
  }
  if (ok) {
    e = ids_.find_gid(g);
    if (e != nullptr && (e->kind != gk_abi_data || e->role != kind ||
                         e->element != t)) {
      problem = "G-id " + number(g) + " was introduced as something else "
                "and cannot be re-issued as this DECL_ABI_DATA";
      ok = false;
    } else if (e == nullptr) {
      e = ids_.add_gid(g, gk_abi_data, problem);
      ok = e != nullptr;
    }
  }
  if (ok && is_class) {
    uint64_t key = (kind << 32) | t;
    uint32_t other = ids_.abi_data_of(key);

    if (other != 0 && other != g) {
      problem = "a second DECL_ABI_DATA (G-id " + number(g) + ") for the "
                "same kind and class as G-id " + number(other);
      ok = false;
    } else {
      ids_.set_abi_data(key, static_cast<uint32_t>(g));
    }
  }
  if (ok) {
    e->role = static_cast<uint8_t>(kind);
    e->element = static_cast<uint32_t>(t);
    e->decl_flags = static_cast<uint32_t>(flags);
    ok = apply_name(g, *e, name, linkage, problem);
  }
  if (ok && is_class && kind == eadk_typeinfo &&
      (flags & ECIL_ABI_DATA_DEFINED_HERE)) {
    /* A defined class typeinfo refers to the typeinfo of each direct base:
       those come first, and so do the facts of the base (12.3). */
    const a_class_facts *f = ids_.class_facts(static_cast<uint32_t>(t));

    if (f != nullptr) {
      for (uint32_t base : f->direct_bases) {
        if (ids_.abi_data_of((static_cast<uint64_t>(eadk_typeinfo) << 32) |
                             base) == 0) {
          problem = "the typeinfo of class G-id " + number(t) + " is "
                    "defined here, so the typeinfo of its base G-id " +
                    number(base) + " comes first";
          ok = false;
          break;
        }
        ok = need_class_facts(base, true, "the typeinfo of a derived class "
                              "defined here", problem);
        if (!ok)
          break;
      }
    }
  }
  return ok;
}  /* a_ref_checker::check_abi_data */

bool a_ref_checker::check_exception_record(const a_record &r,
                                           std::string &problem)
/*
CATCH_BEGIN, CATCH_PARAM, THROW_BEGIN, THROW_END, FUNC_EH_SPEC,
ARRAY_PARTIAL_DTOR.
*/
{
  bool ok = true;
  uint64_t v = 0;
  uint64_t w = 0;

  switch (r.opcode) {
  case eop_catch_begin:
    if (r.ops[0].kind != an_operand_kind::nil)
      ok = typeinfo_entry(r.ops[0], false, false, problem) != nullptr;
    break;
  case eop_catch_param: {
    uint64_t kind = 0;
    const a_lid_entry *entry = nullptr;
    const a_gid_entry *te = nullptr;

    operand_u64(r.ops[1], kind);
    if (operand_id(r.ops[0], v))
      entry = ids_.find_lid(v);
    if (entry == nullptr || entry->state != ls_value) {
      problem = "CATCH_PARAM names an L-id that is not a local";
      ok = false;
    } else {
      te = ids_.find_gid(entry->type);
    }
    if (ok && kind == 3) {
      problem = "CATCH_PARAM kind 3 is invalid";
      ok = false;
    } else if (ok && (kind == ecat_trivial_copy ||
                      kind == ecat_non_trivial_copy) &&
               (te == nullptr || te->kind != gk_record)) {
      problem = "CATCH_PARAM copy kinds need a parameter of a record type";
      ok = false;
    } else if (ok && (kind == ecat_reference_to_pointer ||
                      kind == ecat_reference_to_record_pointer)) {
      const a_gid_entry *pointee = te != nullptr && te->kind == gk_ptr
                                       ? ids_.find_gid(te->element) : nullptr;

      if (pointee == nullptr || pointee->kind != gk_ptr) {
        problem = "CATCH_PARAM reference_to_pointer kinds need a parameter "
                  "that is a reference to a pointer";
        ok = false;
      }
    }
    if (ok && kind == ecat_non_trivial_copy) {
      const a_gid_entry *copy = operand_id(r.ops[2], w) ? ids_.find_gid(w)
                                                        : nullptr;
      uint32_t cls = 0;

      if (copy == nullptr || copy->kind != gk_func ||
          !class_of_function(copy, cls) || cls != entry->type) {
        problem = "CATCH_PARAM non_trivial_copy needs the copy constructor "
                  "of the parameter's class (g_copy)";
        ok = false;
      } else {
        ok = need_class_facts(entry->type, true, "CATCH_PARAM", problem);
      }
    } else if (ok && r.ops[2].kind != an_operand_kind::nil) {
      problem = "CATCH_PARAM g_copy is only for non_trivial_copy";
      ok = false;
    } else if (ok && kind == ecat_trivial_copy) {
      ok = need_class_facts(entry->type, false, "CATCH_PARAM", problem);
    }
    break;
  }
  case eop_throw_begin:
    ok = need_object(r.ops[0], "THROW_BEGIN", problem);
    break;
  case eop_throw_end:
    ok = typeinfo_entry(r.ops[0], false, false, problem) != nullptr;
    if (ok && operand_id(r.ops[1], w)) {
      const a_gid_entry *d = ids_.find_gid(w);

      if (d == nullptr || d->kind != gk_func) {
        problem = "THROW_END destructor G-id " + number(w) + " is not a "
                  "declared function";
        ok = false;
      }
    }
    break;
  case eop_func_eh_spec: {
    an_array_reader reader = array_elements(r.ops[1]);
    uint64_t kind = 0;

    operand_u64(r.ops[0], kind);
    if (kind == eesk_noexcept && r.ops[1].count != 0) {
      problem = "FUNC_EH_SPEC noexcept has no type list";
      ok = false;
    }
    while (ok && next_uint_element(reader, v)) {
      const a_gid_entry *e = ids_.find_gid(v);

      if (e == nullptr || e->kind != gk_abi_data || e->role != eadk_typeinfo) {
        problem = "FUNC_EH_SPEC type G-id " + number(v) + " is not a "
                  "DECL_ABI_DATA typeinfo";
        ok = false;
      }
    }
    break;
  }
  case eop_array_partial_dtor: {
    const a_gid_entry *d = operand_id(r.ops[0], v) ? ids_.find_gid(v)
                                                   : nullptr;

    if (d == nullptr || d->kind != gk_func) {
      problem = "ARRAY_PARTIAL_DTOR names a G-id that is not a declared "
                "function";
      ok = false;
    }
    break;
  }
  default:
    break;
  }
  return ok;
}  /* a_ref_checker::check_exception_record */

bool a_ref_checker::check_member_pointer_record(const a_record &r,
                                                std::string &problem)
/*
PM_FIELD, PM_CALL, PM_CAST, CONST_DATA_MEMBER, CONST_METHOD, C_DATA_MEMBER,
C_METHOD.
*/
{
  bool ok = true;
  uint64_t v = 0;
  uint64_t w = 0;
  const a_gid_entry *t = nullptr;

  switch (r.opcode) {
  case eop_pm_field:
  case eop_const_data_member:
  case eop_c_data_member:
    ok = need_type(r.ops[0], false, problem);
    if (ok && operand_id(r.ops[0], v))
      t = ids_.find_gid(v);
    if (ok && (t == nullptr || t->kind != gk_data_member)) {
      problem = std::string(r.info->name) + " needs a TY_DATA_MEMBER type "
                "(G-id " + number(v) + ")";
      ok = false;
    }
    break;
  case eop_pm_call: {
    uint64_t nargs = 0;

    ok = need_type(r.ops[0], false, problem);
    if (ok && operand_id(r.ops[0], v))
      t = ids_.find_gid(v);
    operand_u64(r.ops[1], nargs);
    if (ok && (t == nullptr || t->kind != gk_func_type)) {
      problem = "PM_CALL needs a function type (G-id " + number(v) + ")";
      ok = false;
    } else if (ok && (nargs == 0 || nargs < t->extent)) {
      problem = "PM_CALL nargs counts the parameters of its function type, "
                "the object pointer included";
      ok = false;
    }
    break;
  }
  case eop_pm_cast: {
    const a_gid_entry *from = nullptr;
    const a_gid_entry *to = nullptr;

    ok = need_type(r.ops[1], false, problem) &&
         need_type(r.ops[2], false, problem);
    if (ok) {
      operand_u64(r.ops[1], v);
      operand_u64(r.ops[2], w);
      from = ids_.find_gid(v);
      to = ids_.find_gid(w);
      if (from == nullptr || to == nullptr ||
          (from->kind != gk_data_member && from->kind != gk_method) ||
          from->kind != to->kind) {
        problem = "PM_CAST converts between two member pointer types of one "
                  "kind (both data member or both method)";
        ok = false;
      }
    }
    break;
  }
  case eop_const_method:
  case eop_c_method: {
    uint64_t flags = 0;
    uint64_t adj = 0;
    const a_gid_entry *fn = nullptr;

    ok = need_type(r.ops[0], false, problem);
    if (ok && operand_id(r.ops[0], v))
      t = ids_.find_gid(v);
    operand_u64(r.ops[2], adj);
    operand_u64(r.ops[3], flags);
    if (ok && (t == nullptr || t->kind != gk_method)) {
      problem = std::string(r.info->name) + " needs a TY_METHOD type "
                "(G-id " + number(v) + ")";
      ok = false;
    }
    if (ok && operand_id(r.ops[1], w)) {
      fn = ids_.find_gid(w);
      if (fn == nullptr || fn->kind != gk_func) {
        problem = std::string(r.info->name) + " names G-id " + number(w) +
                  ", which is not a declared function";
        ok = false;
      }
    }
    if (ok && (flags & ECIL_METHOD_VIRTUAL)) {
      if (fn == nullptr || fn->extent == 0) {
        problem = std::string(r.info->name) + " VIRTUAL needs a function "
                  "that a CLASS_VFUNCS lists";
        ok = false;
      } else {
        ok = need_class_facts(fn->extent, false, "a virtual method constant",
                              problem);
      }
    }
    if (ok && fn == nullptr && (adj != 0 || (flags & ECIL_METHOD_VIRTUAL))) {
      problem = std::string(r.info->name) + ": the null method pointer has "
                "no adjustment and is not virtual";
      ok = false;
    }
    if (ok && r.opcode == eop_c_method && adj != 0 && ids_.in_function()) {
      problem = "C_METHOD with a non-zero adjustment in a function body "
                "(use CONST_METHOD)";
      ok = false;
    }
    break;
  }
  default:
    break;
  }
  return ok;
}  /* a_ref_checker::check_member_pointer_record */

bool a_ref_checker::check_call_flags(const a_record &r, std::string &problem)
/*
CALL flags of 1.2: VIRTUAL needs a listed virtual function with its class
facts; VTT is set exactly when the callee is the base variant of a has_vbases
pair.
*/
{
  bool ok = true;
  uint64_t v = 0;
  uint64_t flags = 0;
  const a_gid_entry *fn = operand_id(r.ops[0], v) ? ids_.find_gid(v) : nullptr;

  operand_u64(r.ops[3], flags);
  if (fn != nullptr && fn->kind == gk_func) {
    bool base_vbases = (fn->role & sr_base) && (fn->role & sr_vbases);

    if ((flags & ECIL_CALL_VIRTUAL) && fn->extent == 0) {
      problem = "CALL VIRTUAL names G-id " + number(v) + ", which no "
                "CLASS_VFUNCS lists";
      ok = false;
    } else if (flags & ECIL_CALL_VIRTUAL) {
      ok = need_class_facts(fn->extent, false, "a virtual call", problem);
    }
    if (ok && ((flags & ECIL_CALL_VTT) != 0) != base_vbases) {
      problem = "CALL VTT is set exactly when the callee is the base "
                "variant of a has_vbases STRUCTOR pair (G-id " + number(v) +
                ")";
      ok = false;
    }
  }
  return ok;
}  /* a_ref_checker::check_call_flags */

bool a_ref_checker::check_m4(const a_record &r, std::string &problem)
/*
The ECIL 1.2 records that need the id tables.
*/
{
  bool ok = true;
  uint64_t v = 0;
  uint64_t w = 0;
  a_gid_entry *ne = nullptr;

  switch (r.opcode) {
  case eop_ty_vptr:
    ok = introduce(r.ops[0], gk_vptr, problem) != nullptr;
    break;
  case eop_ty_data_member:
  case eop_ty_method: {
    bool method = r.opcode == eop_ty_method;
    const a_gid_entry *member = nullptr;
    const a_gid_entry *cls = nullptr;

    ok = need_type(r.ops[1], false, problem) &&
         need_type(r.ops[2], false, problem);
    if (ok) {
      operand_u64(r.ops[1], v);
      operand_u64(r.ops[2], w);
      member = ids_.find_gid(v);
      cls = ids_.find_gid(w);
      if (cls == nullptr || cls->kind != gk_record) {
        problem = std::string(r.info->name) + " names a class G-id that is "
                  "not a record";
        ok = false;
      } else if (method) {
        const a_gid_entry *pt = member->kind == gk_func_type && member->extent
                                    ? ids_.find_gid(member->size) : nullptr;

        if (pt == nullptr || pt->kind != gk_ptr || pt->element != w) {
          problem = "TY_METHOD needs a function type whose first parameter "
                    "points to the class";
          ok = false;
        }
      } else if (member->kind == gk_func_type || member->kind == gk_void) {
        problem = "TY_DATA_MEMBER of a function or void type";
        ok = false;
      }
    }
    if (ok) {
      ne = introduce(r.ops[0], method ? gk_method : gk_data_member, problem);
      ok = ne != nullptr;
      if (ok) {
        ne->element = static_cast<uint32_t>(v);
        ne->extent = w;
      }
    }
    break;
  }
  case eop_class_mangle: {
    const a_gid_entry *rec = nullptr;
    a_class_facts *f = nullptr;

    ok = need_type(r.ops[0], false, problem) &&
         need_sid(r.ops[1], false, "the mangling", problem);
    if (ok && operand_id(r.ops[0], v))
      rec = ids_.find_gid(v);
    if (ok && (rec == nullptr || rec->kind != gk_record)) {
      problem = "CLASS_MANGLE names G-id " + number(v) + ", which is not a "
                "record";
      ok = false;
    } else if (ok) {
      operand_u64(r.ops[1], w);
      if (ids_.string(w).empty()) {
        problem = "CLASS_MANGLE: an empty mangling";
        ok = false;
      } else {
        f = &ids_.add_class_facts(static_cast<uint32_t>(v));
        f->mangle_sid = static_cast<uint32_t>(w);
        f->stage |= cs_mangle;
      }
    }
    break;
  }
  case eop_class_info:
    ok = check_class_info(r, problem);
    break;
  case eop_class_bases:
    ok = check_class_bases(r, problem);
    break;
  case eop_class_vfuncs:
    ok = check_class_vfuncs(r, problem);
    break;
  case eop_class_overrides:
    ok = check_class_overrides(r, problem);
    break;
  case eop_structor_deleting: {
    a_gid_entry *d1 = nullptr;
    a_gid_entry *d0 = nullptr;
    const a_gid_entry *del = nullptr;
    uint32_t cls = 0;
    uint64_t g0 = 0;
    uint64_t g1 = 0;
    uint64_t gd = 0;
    uint64_t sflags = 0;

    operand_u64(r.ops[3], sflags);
    if (operand_id(r.ops[0], g0))
      d0 = ids_.find_gid(g0);
    if (operand_id(r.ops[1], g1))
      d1 = ids_.find_gid(g1);
    if (operand_id(r.ops[2], gd))
      del = ids_.find_gid(gd);
    if (d1 == nullptr || d1->kind != gk_func ||
        (d1->role & (sr_complete | sr_dtor)) != (sr_complete | sr_dtor)) {
      problem = "STRUCTOR_DELETING g_d1 is not the complete variant of a "
                "destructor STRUCTOR";
      ok = false;
    } else if (d1->role & sr_deleting) {
      problem = "STRUCTOR_DELETING of G-id " + number(g1) + " appears twice";
      ok = false;
    } else if (d0 == nullptr || d0->kind != gk_func || d0->role != 0 ||
               d0->element != d1->element || d0->linkage != d1->linkage) {
      problem = "STRUCTOR_DELETING g_d0 is its own DECL_FUNC with the type "
                "and linkage of D1, introduced just before";
      ok = false;
    } else if (del == nullptr || del->kind != gk_func) {
      problem = "STRUCTOR_DELETING g_delete is not a declared function";
      ok = false;
    } else if (!class_of_function(d1, cls) ||
               ids_.class_facts(cls) == nullptr ||
               !ids_.class_facts(cls)->complete) {
      problem = "STRUCTOR_DELETING: the class of the destructor is not "
                "complete (send its TY_RECORD_BODY first)";
      ok = false;
    } else {
      const a_gid_entry *ft = ids_.find_gid(del->element);
      uint64_t params = 1 + ((sflags & ECIL_STRUCTOR_DELETING_SIZED) ? 1 : 0) +
                        ((sflags & ECIL_STRUCTOR_DELETING_ALIGNED) ? 1 : 0);

      if (ft == nullptr || ft->kind != gk_func_type || ft->extent != params) {
        problem = "STRUCTOR_DELETING flags SIZED/ALIGNED do not match the "
                  "parameters of g_delete";
        ok = false;
      }
    }
    if (ok) {
      d1->role |= sr_deleting;
      d0->role = sr_d0;
      ids_.set_deleting(static_cast<uint32_t>(g1), static_cast<uint32_t>(g0),
                        static_cast<uint32_t>(gd));
    }
    break;
  }
  case eop_decl_abi_data:
    ok = check_abi_data(r, problem);
    break;
  case eop_typeinfo_def:
    ok = check_typeinfo_def(r, problem);
    break;
  case eop_vptr_init:
    ok = need_class_facts(fn_class_, false, "VPTR_INIT", problem);
    break;
  case eop_vbase_cast: {
    const a_class_facts *f = nullptr;

    ok = need_type(r.ops[0], false, problem) &&
         need_type(r.ops[1], false, problem);
    if (ok) {
      operand_u64(r.ops[0], v);
      operand_u64(r.ops[1], w);
      f = ids_.class_facts(static_cast<uint32_t>(v));
      if (f == nullptr || !f->complete ||
          !std::binary_search(f->vbases.begin(), f->vbases.end(),
                              static_cast<uint32_t>(w))) {
        problem = "VBASE_CAST: G-id " + number(v) + " has no VIRTUAL_BASE "
                  "field of G-id " + number(w);
        ok = false;
      } else {
        ok = need_class_facts(v, false, "VBASE_CAST", problem);
      }
    }
    break;
  }
  case eop_vtt_addr: {
    uint64_t kind = 0;
    uint64_t idx = 0;
    const a_class_facts *f = fn_class_ != 0 ? ids_.class_facts(fn_class_)
                                            : nullptr;

    operand_u64(r.ops[0], kind);
    if (f == nullptr || f->vbases.empty()) {
      problem = "VTT_ADDR outside a function of a class with virtual bases";
      ok = false;
    } else if (kind == evak_self) {
      if (r.ops[1].kind != an_operand_kind::nil) {
        problem = "VTT_ADDR self has no index";
        ok = false;
      }
    } else if (!operand_u64(r.ops[1], idx) || idx >= f->fields.size() ||
               !(f->fields[idx].flags & (kind == evak_base
                                             ? ECIL_FIELD_BASE
                                             : ECIL_FIELD_VIRTUAL_BASE))) {
      problem = "VTT_ADDR idx is not a field of the right kind (BASE for "
                "base, VIRTUAL_BASE for vbase) of the function's class";
      ok = false;
    }
    if (ok)
      ok = need_class_facts(fn_class_, false, "VTT_ADDR", problem);
    break;
  }
  case eop_dynamic_cast:
  case eop_typeid: {
    bool cast = r.opcode == eop_dynamic_cast;
    const a_gid_entry *src = nullptr;

    if (!(ids_.lang_flags() & ECIL_LANG_RTTI)) {
      problem = std::string(r.info->name) + " needs rtti = 1 in HDR_LANG";
      ok = false;
    }
    if (ok && (cast || r.ops[0].kind != an_operand_kind::nil)) {
      src = typeinfo_entry(r.ops[0], cast, false, problem);
      ok = src != nullptr;
      if (ok && cast)
        ok = need_class_facts(src->element, false, "DYNAMIC_CAST", problem);
    }
    if (ok && cast && r.ops[1].kind != an_operand_kind::nil) {
      const a_gid_entry *dst = typeinfo_entry(r.ops[1], true, false, problem);

      ok = dst != nullptr;
      if (ok)
        ok = need_class_facts(dst->element, false, "DYNAMIC_CAST", problem);
    }
    if (ok)
      ok = need_type(r.ops[cast ? 2 : 1], false, problem);
    break;
  }
  case eop_pm_field:
  case eop_pm_call:
  case eop_pm_cast:
  case eop_const_data_member:
  case eop_const_method:
  case eop_c_data_member:
  case eop_c_method:
    ok = check_member_pointer_record(r, problem);
    break;
  case eop_catch_begin:
  case eop_catch_param:
  case eop_throw_begin:
  case eop_throw_end:
  case eop_func_eh_spec:
  case eop_array_partial_dtor:
    ok = check_exception_record(r, problem);
    break;
  default:
    break;
  }
  return ok;
}  /* a_ref_checker::check_m4 */

bool a_ref_checker::check_end(std::string &problem)
/*
At END: every dynamic class has CLASS_INFO, every typeinfo of a
type that is not a class has its TYPEINFO_DEF.
*/
{
  bool ok = true;

  for (const auto &entry : ids_.all_class_facts()) {
    if (ok && entry.second.dynamic && !(entry.second.stage & cs_info)) {
      problem = "class G-id " + number(entry.first) + " is dynamic but has "
                "no CLASS_INFO";
      ok = false;
    }
  }
  /* D0 is defined iff D1 is defined here (STRUCTOR_DELETING). */
  for (const auto &entry : ids_.all_deleting()) {
    const a_gid_entry *d1 = ids_.find_gid(entry.first);
    const a_gid_entry *d0 = ids_.find_gid(entry.second.first);

    if (ok && d1 != nullptr && d0 != nullptr &&
        ((d1->decl_flags & ECIL_DECL_FUNC_DEFINED_HERE) !=
         (d0->decl_flags & ECIL_DECL_FUNC_DEFINED_HERE))) {
      problem = "D0 (G-id " + number(entry.second.first) + ") is defined "
                "here if and only if its D1 (G-id " + number(entry.first) +
                ") is";
      ok = false;
    }
  }
  for (uint64_t g = 1; ok && g < ids_.gid_table_size(); ++g) {
    const a_gid_entry *e = ids_.find_gid(g);

    if (e != nullptr && e->kind == gk_abi_data && e->role == eadk_typeinfo &&
        !(e->flags & gf_defined)) {
      const a_gid_entry *t = ids_.find_gid(e->element);

      if (t != nullptr && t->kind != gk_record) {
        problem = "the typeinfo G-id " + number(g) + " of a type that is not "
                  "a class has no TYPEINFO_DEF";
        ok = false;
      }
    }
  }
  return ok;
}  /* a_ref_checker::check_end */

}  /* namespace ecil */
