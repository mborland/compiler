/*
Part of the EDG Compiler Project, under the Apache License v2.0 with LLVM
Exceptions.
See https://edgcpp.org/LICENSE.txt for license information.
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
*/

/*
ecil_ids.cpp -- the id tables (see ecil_ids.h).
*/

#include <cstring>

#include "ecil_ids.h"

namespace ecil {

namespace {

std::string number(uint64_t v)
/*
Decimal text of v.
*/
{
  return std::to_string(static_cast<unsigned long long>(v));
}  /* number */

bool within_limit(uint64_t id, uint64_t introduced)
/*
The reader limit of spec section 7 (written so that it cannot overflow).
*/
{
  return id <= 2 * introduced + id_slack;
}  /* within_limit */

}  /* anonymous namespace */

std::string_view a_string_arena::store(const char *data, size_t length)
/*
Copy a string into the current chunk (a new one when it does not fit).
*/
{
  std::string_view result;

  if (length != 0) {
    if (length > left_) {
      size_t size = length > chunk_size ? length : chunk_size;
      chunks_.emplace_back(new char[size]);
      cursor_ = chunks_.back().get();
      left_ = size;
    }
    std::memcpy(cursor_, data, length);
    result = std::string_view(cursor_, length);
    cursor_ += length;
    left_ -= length;
  }
  return result;
}  /* a_string_arena::store */

a_id_tables::a_id_tables()
  : gids_introduced_(0), sids_introduced_(0)
{
  strings_.resize(1);
  present_.resize(1, 0);
  gids_.resize(1);
  std::memset(&gids_[0], 0, sizeof gids_[0]);
}  /* a_id_tables::a_id_tables */

bool a_id_tables::add_string(uint64_t sid, const uint8_t *data, size_t length,
                             std::string &problem)
/*
Introduce a string id, or accept a repeat of the same contents.
*/
{
  bool ok = true;

  if (sid == 0) {
    problem = "string id 0 cannot be introduced";
    ok = false;
  } else if (has_string(sid)) {
    if (strings_[sid].size() != length ||
        (length != 0 &&
         std::memcmp(strings_[sid].data(), data, length) != 0)) {
      problem = "string id " + number(sid) + " is introduced twice with "
                "different contents";
      ok = false;
    }
  } else if (!within_limit(sid, sids_introduced_)) {
    problem = "string id " + number(sid) + " is far above the " +
              number(sids_introduced_) + " introduced so far (ids must be "
              "dense)";
    ok = false;
  } else {
    if (sid >= strings_.size()) {
      strings_.resize(sid + 1);
      present_.resize(sid + 1, 0);
    }
    strings_[sid] = arena_.store(reinterpret_cast<const char *>(data),
                                 length);
    present_[sid] = 1;
    ++sids_introduced_;
  }
  return ok;
}  /* a_id_tables::add_string */

a_gid_entry *a_id_tables::add_gid(uint64_t g, a_gid_kind kind,
                                  std::string &problem)
/*
Introduce a G-id.
*/
{
  a_gid_entry *result = nullptr;

  if (g == 0) {
    problem = "G-id 0 cannot be introduced";
  } else if (g < gids_.size() && gids_[g].kind != gk_none) {
    problem = "G-id " + number(g) + " is introduced twice";
  } else if (!within_limit(g, gids_introduced_)) {
    problem = "G-id " + number(g) + " is far above the " +
              number(gids_introduced_) + " introduced so far (ids must be "
              "dense)";
  } else {
    if (g >= gids_.size()) {
      a_gid_entry blank;
      std::memset(&blank, 0, sizeof blank);
      gids_.resize(g + 1, blank);
    }
    result = &gids_[g];
    std::memset(result, 0, sizeof *result);
    result->kind = kind;
    ++gids_introduced_;
  }
  return result;
}  /* a_id_tables::add_gid */

void a_id_tables::begin_function()
/*
Start the L-id table of a function body.
*/
{
  function_bases_.push_back(lids_.size());
  function_seen_.push_back(0);
}  /* a_id_tables::begin_function */

void a_id_tables::end_function()
/*
Drop the L-ids of the innermost function.
*/
{
  if (!function_bases_.empty()) {
    lids_.resize(function_bases_.back());
    function_bases_.pop_back();
    function_seen_.pop_back();
  }
}  /* a_id_tables::end_function */

a_lid_entry *a_id_tables::peek_lid(uint64_t l)
/*
The entry of L-id l in the current function, whatever its state.
*/
{
  a_lid_entry *result = nullptr;

  if (!function_bases_.empty() && l != 0 &&
      l < lids_.size() - function_bases_.back())
    result = &lids_[function_bases_.back() + l];
  return result;
}  /* a_id_tables::peek_lid */

a_lid_entry *a_id_tables::touch_lid(uint64_t l, std::string &problem)
/*
The entry of L-id l, created when new; the first touch counts as a sighting
for the reader limit.
*/
{
  a_lid_entry *result = nullptr;

  if (function_bases_.empty()) {
    problem = "local id used outside a function";
  } else if (l == 0) {
    problem = "L-id 0 cannot be introduced";
  } else {
    size_t base = function_bases_.back();
    uint64_t seen = function_seen_.back();

    result = peek_lid(l);
    if (result == nullptr) {
      if (!within_limit(l, seen)) {
        problem = "L-id " + number(l) + " is far above the " + number(seen) +
                  " seen so far in the function (ids must be dense)";
      } else {
        a_lid_entry blank;
        std::memset(&blank, 0, sizeof blank);
        lids_.resize(base + l + 1, blank);
        result = &lids_[base + l];
      }
    }
    if (result != nullptr && !result->seen) {
      if (!within_limit(l, seen)) {
        problem = "L-id " + number(l) + " is far above the " + number(seen) +
                  " seen so far in the function (ids must be dense)";
        result = nullptr;
      } else {
        result->seen = 1;
        ++function_seen_.back();
      }
    }
  }
  return result;
}  /* a_id_tables::touch_lid */

bool a_id_tables::bind_symbol(std::string_view text, uint32_t g,
                              uint32_t &other)
/*
Claim an external symbol name for g.
*/
{
  auto inserted = owners_.try_emplace(text, g);

  other = inserted.first->second;
  return inserted.second || inserted.first->second == g;
}  /* a_id_tables::bind_symbol */

void a_id_tables::release_symbol(std::string_view text, uint32_t g)
/*
Give up a symbol name that g owns.
*/
{
  auto found = owners_.find(text);

  if (found != owners_.end() && found->second == g)
    owners_.erase(found);
}  /* a_id_tables::release_symbol */

}  /* namespace ecil */
