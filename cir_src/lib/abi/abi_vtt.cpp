/*
Part of the EDG Compiler Project, under the Apache License v2.0 with LLVM
Exceptions.
See https://edgcpp.org/LICENSE.txt for license information.
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
*/

/*
abi_vtt.cpp -- the virtual table table (VTT) layout.

Derived in part from clang/lib/AST/VTTBuilder.cpp, which is part of the LLVM
Project, under the Apache License v2.0 with LLVM Exceptions
(https://llvm.org/LICENSE.txt, SPDX-License-Identifier:
Apache-2.0 WITH LLVM-exception).  What changed: the input is the class-fact
tables of abi_facts.h; the recursions of LayoutVTT, LayoutSecondaryVTTs,
LayoutSecondaryVirtualPointers and LayoutVirtualVTTs run on heap stacks.
*/

#include "abi_vtable.h"

#include <set>

namespace edg_abi {

namespace {

std::string number(int64_t v)
/*
Decimal text of v.
*/
{
  return std::to_string(static_cast<long long>(v));
}  /* number */

class a_vtt_builder {
public:
  a_vtt_builder(const a_class_db &db, a_class_id most_derived,
                bool generate_definition, a_vtt_layout &out);

  bool run(std::string &error);

private:
  void layout_vtt(const a_subobject &base, bool base_is_virtual,
                  bool is_root);
  void layout_secondary_virtual_pointers(const a_subobject &base,
                                         uint64_t vtable_index,
                                         a_class_id vtable_class);
  void layout_virtual_vtts();
  void add_vtable_pointer(const a_subobject &base, uint64_t vtable_index,
                          a_class_id vtable_class);
  void fail(const std::string &message);

  const a_class_db  &db_;
  a_class_id         md_;
  bool               generate_definition_;
  a_vtt_layout      &out_;
  std::string        error_;
};  /* a_vtt_builder */

a_vtt_builder::a_vtt_builder(const a_class_db &db, a_class_id most_derived,
                             bool generate_definition, a_vtt_layout &out)
  : db_(db), md_(most_derived), generate_definition_(generate_definition),
    out_(out)
{
}  /* a_vtt_builder::a_vtt_builder */

void a_vtt_builder::fail(const std::string &message)
/*
Remember the first problem.
*/
{
  if (error_.empty())
    error_ = message;
}  /* a_vtt_builder::fail */

bool a_vtt_builder::run(std::string &error)
/*
VTTBuilder's constructor: LayoutVTT of the most derived class.
*/
{
  layout_vtt(a_subobject{md_, 0}, false, true);
  error = error_;
  return error_.empty();
}  /* a_vtt_builder::run */

void a_vtt_builder::add_vtable_pointer(const a_subobject &base,
                                       uint64_t vtable_index,
                                       a_class_id vtable_class)
/*
AddVTablePointer: one VTT component for the address point of `base` in the
vtable (group) number vtable_index.
*/
{
  a_vtt_component comp;

  if (vtable_class == md_) {
    if (out_.secondary_virtual_pointer_indices.count(base) != 0)
      fail("a virtual pointer index exists already for a base subobject");
    out_.secondary_virtual_pointer_indices[base] = out_.components.size();
  }
  comp.vtable_index = generate_definition_ ? vtable_index : 0;
  comp.vtable_base = generate_definition_ ? base : a_subobject{no_id, 0};
  comp.present = generate_definition_;
  out_.components.push_back(comp);
}  /* a_vtt_builder::add_vtable_pointer */

void a_vtt_builder::layout_vtt(const a_subobject &root, bool root_is_virtual,
                               bool is_root)
/*
LayoutVTT with LayoutSecondaryVTTs: the VTT part of a base subobject that has
virtual bases: its own vtable pointer, the VTT parts of its non-virtual bases,
its secondary virtual pointers and, for the complete class, the VTT parts of
the virtual bases.
*/
{
  struct a_frame {
    a_subobject  base;
    bool         is_virtual;
    size_t       next;
    uint64_t     vtable_index;
    bool         entered;
  };  /* a_frame */
  std::vector<a_frame> stack;

  if (db_.cls(root.cls).vbases.empty())
    return;
  stack.push_back({root, root_is_virtual, 0, 0, false});
  while (error_.empty() && !stack.empty()) {
    a_frame &top = stack.back();
    const a_class_fact &c = db_.cls(top.base.cls);

    if (!top.entered) {
      top.entered = true;
      if (top.base.cls != md_)
        out_.sub_vtt_indices[top.base] = out_.components.size();
      top.vtable_index = out_.vtables.size();
      out_.vtables.push_back(a_vtt_vtable{top.base, top.is_virtual});
      add_vtable_pointer(top.base, top.vtable_index, top.base.cls);
    } else if (top.next < c.bases.size()) {
      const a_base_fact &b = c.bases[top.next++];

      if (!b.is_virtual && !db_.cls(b.cls).vbases.empty()) {
        a_frame child = {a_subobject{b.cls, top.base.offset + b.offset},
                         false, 0, 0, false};

        stack.push_back(child);
      }
    } else {
      a_subobject base = top.base;
      uint64_t vtable_index = top.vtable_index;
      a_class_id cls = top.base.cls;

      stack.pop_back();
      layout_secondary_virtual_pointers(base, vtable_index, base.cls);
      if (cls == md_ && error_.empty())
        layout_virtual_vtts();
    }
  }
  (void)is_root;
}  /* a_vtt_builder::layout_vtt */

void a_vtt_builder::layout_secondary_virtual_pointers(
    const a_subobject &root, uint64_t vtable_index, a_class_id vtable_class)
/*
LayoutSecondaryVirtualPointers: a vtable pointer for every dynamic base of
the subobject that has virtual bases or is (morally) virtual and is not the
non-virtual primary base of its parent.
*/
{
  struct a_frame {
    a_subobject  base;
    bool         morally_virtual;
    size_t       next;
  };  /* a_frame */
  std::vector<a_frame> stack;
  std::set<a_class_id> vbases;

  if (db_.cls(root.cls).vbases.empty())
    return;
  stack.push_back({root, false, 0});
  while (error_.empty() && !stack.empty()) {
    a_frame &top = stack.back();
    const a_class_fact &c = db_.cls(top.base.cls);

    if (top.next >= c.bases.size()) {
      stack.pop_back();
      continue;
    }
    const a_base_fact &b = c.bases[top.next++];

    if (!db_.cls(b.cls).is_dynamic)
      continue;
    bool morally = top.morally_virtual;
    bool nonvirtual_primary = false;
    int64_t offset = 0;

    if (b.is_virtual) {
      if (!vbases.insert(b.cls).second)
        continue;
      if (!db_.find_vbase_offset(md_, b.cls, offset)) {
        fail("virtual base " + number(b.cls) + " missing from class " +
             number(md_));
        continue;
      }
      morally = true;
    } else {
      offset = top.base.offset + b.offset;
      if (!c.primary_virtual && c.primary == b.cls)
        nonvirtual_primary = true;
    }
    a_subobject sub{b.cls, offset};

    if (!nonvirtual_primary && (!db_.cls(b.cls).vbases.empty() || morally))
      add_vtable_pointer(sub, vtable_index, vtable_class);
    /* The recursion returns at once for a class without virtual bases that
       is not morally virtual. */
    if (!db_.cls(b.cls).vbases.empty() || morally)
      stack.push_back({sub, morally, 0});
  }
}  /* a_vtt_builder::layout_secondary_virtual_pointers */

void a_vtt_builder::layout_virtual_vtts()
/*
LayoutVirtualVTTs: the VTT parts of the virtual bases of the complete class.
*/
{
  struct a_frame {
    a_class_id  cls;
    size_t      next;
  };  /* a_frame */
  std::vector<a_frame> stack;
  std::set<a_class_id> vbases;

  stack.push_back({md_, 0});
  while (error_.empty() && !stack.empty()) {
    a_frame &top = stack.back();
    const a_class_fact &c = db_.cls(top.cls);

    if (top.next >= c.bases.size()) {
      stack.pop_back();
    } else {
      const a_base_fact &b = c.bases[top.next++];

      if (b.is_virtual) {
        int64_t offset = 0;

        if (!vbases.insert(b.cls).second)
          continue;
        if (!db_.find_vbase_offset(md_, b.cls, offset)) {
          fail("virtual base missing from the most derived class");
          continue;
        }
        layout_vtt(a_subobject{b.cls, offset}, true, false);
      }
      if (error_.empty() && !db_.cls(b.cls).vbases.empty())
        stack.push_back({b.cls, 0});
    }
  }
}  /* a_vtt_builder::layout_virtual_vtts */

}  /* anonymous namespace */

std::unique_ptr<a_vtt_layout> a_vtable_context::vtt_layout(
    a_class_id c, bool generate_definition, std::string &error)
/*
The VTT of class c.
*/
{
  std::unique_ptr<a_vtt_layout> result(new a_vtt_layout);
  a_vtt_builder builder(db_, c, generate_definition, *result);

  if (c >= db_.classes.size()) {
    error = "unknown class " + number(c);
    result.reset();
  } else if (!builder.run(error)) {
    result.reset();
  }
  return result;
}  /* a_vtable_context::vtt_layout */

}  /* namespace edg_abi */
