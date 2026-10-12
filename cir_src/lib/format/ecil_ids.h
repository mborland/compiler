/*
Part of the EDG Compiler Project, under the Apache License v2.0 with LLVM
Exceptions.
See https://edgcpp.org/LICENSE.txt for license information.
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
*/

/*
ecil_ids.h -- the LLVM-free id tables of the CIR library: one dense table per
id space (strings, G-ids, L-ids) plus the external-symbol owner map, all
owned by the stream reader.  The validator fills them while it checks a
record; the emitter reads them (and keeps its own objects in the payload
slots) instead of keeping tables of its own, so every reference is looked up
by index once per stage and never hashed.

Growth is bounded by the stream (spec section 7): an id above
2 * introduced + id_slack is rejected, so a table is never longer than that
and lookups never resize.  Nothing here recurses or asserts.
*/

#ifndef ECIL_IDS_H
#define ECIL_IDS_H 1

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ecil {

/* The reader limit of spec section 7: a new id may be at most
   2 * introduced + id_slack, where introduced counts the ids of that space
   introduced so far. */
const uint64_t id_slack = 1024;

/* Strings are copied once into chunks; views stay valid as long as the arena
   lives. */
class a_string_arena {
public:
  static const size_t chunk_size = 65536;

  std::string_view store(const char *data, size_t length);
  /* Copy length bytes (a string longer than a chunk gets a chunk of its
     own) and return a view of the copy. */

  size_t chunk_count() const
    { return chunks_.size(); }

private:
  std::vector<std::unique_ptr<char[]>> chunks_;
  char                                *cursor_ = nullptr;
  size_t                               left_ = 0;
};  /* a_string_arena */

/* G-ids. */

enum a_gid_kind : uint8_t {
  gk_none = 0,   /* Not introduced. */
  gk_void, gk_bool, gk_int, gk_float, gk_ptr, gk_array, gk_func_type,
  gk_record, gk_opaque, gk_func, gk_var,
  gk_vptr,         /* TY_VPTR (1.2). */
  gk_data_member,  /* TY_DATA_MEMBER: element = member type, extent = class. */
  gk_method,       /* TY_METHOD: element = function type, extent = class. */
  gk_abi_data      /* DECL_ABI_DATA: role = kind, element = t. */
};  /* a_gid_kind */

enum a_gid_flag : uint8_t {
  gf_complete  = 1,   /* Record with a body. */
  gf_union     = 2,
  gf_has_count = 4,   /* Array with a count. */
  gf_named     = 8,   /* Declaration: the effective name is set. */
  gf_defined   = 16,  /* Function: FUNC_BEGIN seen.  Variable: defining. */
  gf_owns_name = 32,  /* Declaration: owns its external symbol name. */
  gf_in_group  = 128  /* Function: member of a STRUCTOR pair. */
};  /* a_gid_flag */

/* One G-id (index = the id).  Types use element and extent; function and
   variable declarations keep the facts the effective-name and definition
   rules need (spec section 7), the declaration side table. */
struct a_gid_entry {
  uint8_t   kind;      /* a_gid_kind. */
  uint8_t   flags;     /* a_gid_flag bits. */
  uint8_t   linkage;   /* Declaration: the latest linkage. */
  uint8_t   role;      /* STRUCTOR: 1 complete (C1/D1), 2 base (C2/D2). */
  uint32_t  element;   /* Array: element G-id.  Declaration: its type.
                          Function type: the return type. */
  uint64_t  extent;    /* Record: field count.  Array: count.  Int: bits. */
  uint64_t  size;      /* Bool, int, float, opaque and record: the size in
                          bytes the stream gave (arrays and pointers are
                          derived by the layout check). */
  uint32_t  name_sid;  /* Declaration: the effective name's sid (0: none). */
  uint32_t  decl_flags;  /* Declaration: the latest DECL_* flags. */
  void     *payload;   /* Owner data (the emitter's objects); null in
                          ecil-dump. */
};  /* a_gid_entry */

/* a_gid_entry::role of a function: the STRUCTOR role bits. */
enum a_structor_role : uint8_t {
  sr_complete = 1,    /* C1/D1. */
  sr_base     = 2,    /* C2/D2. */
  sr_vbases   = 4,    /* The pair has virtual bases (1.2); both variants. */
  sr_dtor     = 8,    /* A destructor pair (1.2); both variants. */
  sr_deleting = 16,   /* D1 with a STRUCTOR_DELETING (1.2). */
  sr_d0       = 32    /* A D0 named by a STRUCTOR_DELETING (1.2). */
};  /* a_structor_role */

/* a_gid_entry::role of a variable. */
enum a_var_role : uint8_t {
  vr_var_init    = 1,   /* VAR_INIT seen. */
  vr_static_init = 2    /* STATIC_INIT_BEGIN seen (at most one per variable;
                           with a VAR_INIT it may hold only the exit
                           cleanup). */
};  /* a_var_role */

/* a_gid_entry::decl_flags bit above the DECL_* flags: the declaration was
   introduced with a nil name (NAME_BIND may name it later). */
const uint32_t decl_flag_nil_introduced = 0x80000000u;

/* L-ids. */

enum a_lid_state : uint8_t {
  ls_unseen = 0,
  ls_value,    /* PARAM, LOCAL, MATERIALIZE_TEMP. */
  ls_label,    /* LABEL. */
  ls_dead      /* A value whose scope region has closed. */
};  /* a_lid_state */

enum a_lid_flag : uint8_t {
  lf_goto_target = 1   /* Named by a GOTO_STMT. */
};  /* a_lid_flag */

/* One L-id of the current function (index = base + the id). */
struct a_lid_entry {
  uint8_t   state;     /* a_lid_state. */
  uint8_t   flags;     /* a_lid_flag bits. */
  uint8_t   seen;      /* Introduced or named by a GOTO (counts for the
                          reader limit). */
  uint32_t  type;      /* PARAM (declared), LOCAL, MATERIALIZE_TEMP: the type
                          G-id. */
  void     *payload;
};  /* a_lid_entry */

/* The class facts of a record (ECIL 1.2): its fields, as the validator needs
   them, and what the CLASS_* records said. */
struct a_class_field {
  uint32_t  type;      /* The field type G-id. */
  uint64_t  offset;
  uint8_t   flags;     /* ECIL_FIELD_* of the REC_FIELD. */
};  /* a_class_field */

enum a_class_stage : uint8_t {
  cs_mangle    = 1,
  cs_info      = 2,
  cs_bases     = 4,
  cs_vfuncs    = 8,
  cs_overrides = 16
};  /* a_class_stage */

struct a_class_facts {
  std::vector<a_class_field>  fields;
  uint64_t   size = 0;
  uint64_t   data_size = 0;
  uint64_t   align = 0;
  uint64_t   non_vbase_end = 0;   /* End of the last field that is not a
                                     virtual base (EMPTY_FOR_ABI: 0). */
  uint64_t   nvsize = 0;          /* CLASS_INFO; else data_size. */
  uint64_t   nslots = 0;
  uint32_t   mangle_sid = 0;
  uint8_t    stage = 0;           /* a_class_stage bits. */
  bool       complete = false;    /* All REC_FIELDs seen. */
  bool       has_vptr = false;    /* A TY_VPTR field. */
  bool       has_base_fields = false;
  bool       dynamic = false;     /* A vptr, a virtual base, or a dynamic
                                     base (structural). */
  std::vector<uint32_t>  vbases;  /* Classes of the VIRTUAL_BASE fields,
                                     sorted. */
  std::vector<uint32_t>  direct_bases;  /* Classes of the direct bases
                                     (CLASS_BASES), in its order. */
};  /* a_class_facts */

class a_id_tables {
public:
  a_id_tables();

  /* Strings.  The first STR of an id adds it; a repeated STR with the same
     contents is accepted and changes nothing. */
  bool add_string(uint64_t sid, const uint8_t *data, size_t length,
                  std::string &problem);
  bool has_string(uint64_t sid) const
    { return sid != 0 && sid < strings_.size() && present_[sid]; }
  std::string_view string(uint64_t sid) const
    { return has_string(sid) ? strings_[sid] : std::string_view(); }
  std::string_view store_string(std::string_view text)
    { return arena_.store(text.data(), text.size()); }

  /* G-ids. */
  a_gid_entry *find_gid(uint64_t g)
  /* The introduced entry for g, or nullptr. */
    {
      return g != 0 && g < gids_.size() && gids_[g].kind != gk_none
                 ? &gids_[g] : nullptr;
    }
  const a_gid_entry *find_gid(uint64_t g) const
    {
      return g != 0 && g < gids_.size() && gids_[g].kind != gk_none
                 ? &gids_[g] : nullptr;
    }
  uint64_t gid_table_size() const
    { return gids_.size(); }
  a_gid_entry *add_gid(uint64_t g, a_gid_kind kind, std::string &problem);
  /* Introduce g (once); nullptr with problem set when g is 0, introduced
     already or beyond the reader limit. */

  /* L-ids of the current function (a stack of functions; nested bodies are
     rejected). */
  void begin_function();
  void end_function();
  /* Drop the current function's L-ids. */
  bool in_function() const
    { return !function_bases_.empty(); }
  a_lid_entry *find_lid(uint64_t l)
  /* The seen entry (any state but unseen) of the current function. */
    {
      a_lid_entry *entry = peek_lid(l);
      return entry != nullptr && entry->state != ls_unseen ? entry : nullptr;
    }
  a_lid_entry *peek_lid(uint64_t l);
  /* The entry for l whatever its state, or nullptr when beyond the table. */
  a_lid_entry *touch_lid(uint64_t l, std::string &problem);
  /* The entry for l, created if needed; it counts as seen for the reader
     limit.  nullptr with problem set when l is 0 or too far. */
  size_t function_lid_begin() const
    { return function_bases_.back(); }
  size_t lid_table_size() const
    { return lids_.size(); }
  a_lid_entry &lid_at_index(size_t index)
    { return lids_[index]; }
  size_t lid_index(const a_lid_entry *entry) const
    { return static_cast<size_t>(entry - lids_.data()); }

  /* The Itanium data size of a record that differs from its size. */
  void set_record_data_size(uint32_t record, uint64_t data_size)
    { data_sizes_[record] = data_size; }
  uint64_t record_data_size(uint32_t record, uint64_t size) const
    {
      auto it = data_sizes_.find(record);
      return it == data_sizes_.end() ? size : it->second;
    }

  /* Class facts (ECIL 1.2). */
  a_class_facts &add_class_facts(uint32_t record)
    { return classes_[record]; }
  a_class_facts *class_facts(uint32_t record)
    {
      auto it = classes_.find(record);
      return it == classes_.end() ? nullptr : &it->second;
    }
  const a_class_facts *class_facts(uint32_t record) const
    {
      auto it = classes_.find(record);
      return it == classes_.end() ? nullptr : &it->second;
    }
  const std::unordered_map<uint32_t, a_class_facts> &all_class_facts() const
    { return classes_; }
  /* The one ABI-data G-id of a class kind (kind << 32 | class), 0: none. */
  uint32_t abi_data_of(uint64_t key) const
    {
      auto it = abi_keys_.find(key);
      return it == abi_keys_.end() ? 0 : it->second;
    }
  void set_abi_data(uint64_t key, uint32_t g)
    { abi_keys_[key] = g; }
  /* STRUCTOR_DELETING: D1 -> (D0, operator delete). */
  void set_deleting(uint32_t d1, uint32_t d0, uint32_t del)
    { deleting_[d1] = std::make_pair(d0, del); }
  const std::unordered_map<uint32_t, std::pair<uint32_t, uint32_t>> &
  all_deleting() const
    { return deleting_; }
  bool deleting_of(uint32_t d1, uint32_t &d0, uint32_t &del) const
    {
      auto it = deleting_.find(d1);
      bool found = it != deleting_.end();
      if (found) {
        d0 = it->second.first;
        del = it->second.second;
      }
      return found;
    }
  /* The language flags of HDR_LANG (a C stream: 0). */
  void set_lang(uint32_t language, uint32_t flags)
    {
      lang_ = language;
      lang_flags_ = flags;
    }
  uint32_t lang_flags() const
    { return lang_flags_; }

  /* Base subobjects (REC_FIELD with BASE) of the records, for the casts. */
  void add_base(uint32_t record, uint32_t base, uint64_t offset);
  bool has_base(uint32_t record, uint32_t base, uint64_t offset) const;

  /* External symbol names. */
  bool bind_symbol(std::string_view text, uint32_t g, uint32_t &other);
  /* Claim text for g.  False (and the other owner) when another G-id owns
     it. */
  void release_symbol(std::string_view text, uint32_t g);
  /* Give text up if g owns it. */

  /* Statistics. */
  uint64_t gids_introduced() const
    { return gids_introduced_; }
  uint64_t gid_holes() const
    { return gids_.size() > 1 ? gids_.size() - 1 - gids_introduced_ : 0; }
  uint64_t sids_introduced() const
    { return sids_introduced_; }
  uint64_t sid_holes() const
    {
      return strings_.size() > 1 ? strings_.size() - 1 - sids_introduced_
                                 : 0;
    }

private:
  a_string_arena                         arena_;
  std::vector<std::string_view>          strings_;
  std::vector<uint8_t>                   present_;
  std::vector<a_gid_entry>               gids_;
  std::vector<a_lid_entry>               lids_;
  std::vector<size_t>                    function_bases_;
  std::vector<uint64_t>                  function_seen_;  /* per function */
  std::unordered_map<std::string_view, uint32_t>
                                         owners_;
  struct a_base_rec {
    uint32_t base;
    uint64_t offset;
  };  /* a_base_rec */
  std::unordered_map<uint32_t, std::vector<a_base_rec>>
                                         bases_;
  std::unordered_map<uint32_t, uint64_t> data_sizes_;
  std::unordered_map<uint32_t, a_class_facts> classes_;
  std::unordered_map<uint64_t, uint32_t> abi_keys_;
  std::unordered_map<uint32_t, std::pair<uint32_t, uint32_t>> deleting_;
  uint32_t                               lang_ = 0;
  uint32_t                               lang_flags_ = 0;
  uint64_t                               gids_introduced_;
  uint64_t                               sids_introduced_;
};  /* a_id_tables */

}  /* namespace ecil */

#endif /* ECIL_IDS_H */
