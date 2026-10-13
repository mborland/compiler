/*
Part of the EDG Compiler Project, under the Apache License v2.0 with LLVM
Exceptions.
See https://edgcpp.org/LICENSE.txt for license information.
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
*/

/*
abi_decode_tests -- tests of the decoder from the validator's class facts to
the class-fact tables of lib/abi (ecil_abi_decode.h), on small ECIL text
streams.  LLVM-free.

  abi_decode_tests
*/

#include <cstdio>
#include <string>
#include <vector>

#include "abi_facts.h"
#include "ecil_abi_decode.h"
#include "ecil_reader.h"
#include "ecil_text.h"

namespace {

int failures = 0;

void check(bool condition, const std::string &what)
/*
Record a failure.
*/
{
  if (!condition) {
    ++failures;
    std::fprintf(stderr, "FAIL %s\n", what.c_str());
  }
}  /* check */

class a_null_handler : public ecil::a_stream_handler {
public:
  bool on_preamble(const ecil::a_preamble &) override
    { return true; }
  bool on_frame_begin(const ecil::a_frame_info &) override
    { return true; }
  bool on_record(const ecil::a_record &,
                 const ecil::a_record_context &) override
    { return true; }
  bool on_frame_end(const ecil::a_frame_info &) override
    { return true; }
  bool on_extension_frame(const ecil::a_frame_info &) override
    { return true; }
};  /* a_null_handler */

const char *header =
  "ecil 1.2 features=0x00000000\n"
  "frame HEADER\n"
  "  STR 1 \"x86_64-unknown-linux-gnu\"\n"
  "  HDR_TARGET 1 nil nil 64 64 6\n"
  "  HDR_LANG 1 201703 0\n";

bool decode(const std::string &text, edg_abi::a_class_db &db,
            std::string &problem)
/*
Assemble, read (with checking) and decode a stream.
*/
{
  std::vector<uint8_t> bytes;
  std::string error;
  bool ok = ecil::assemble_text(text, bytes, error);

  if (!ok) {
    problem = "assemble: " + error;
  } else {
    a_null_handler handler;
    ecil::a_stream_reader reader(handler);

    ok = reader.consume(bytes.data(), bytes.size()) && reader.finish();
    if (!ok)
      problem = "read: " + reader.error().message;
    else
      ok = ecil::build_abi_facts(reader.ids(), db, problem);
  }
  return ok;
}  /* decode */

edg_abi::a_class_id find(const edg_abi::a_class_db &db, uint64_t key)
/*
The dense id of the class with the stream id key.
*/
{
  edg_abi::a_class_id result = edg_abi::no_id;

  for (edg_abi::a_class_id c = 0; c < db.classes.size(); ++c) {
    if (db.classes[c].key == key)
      result = c;
  }
  return result;
}  /* find */

void test_plain_class_gets_info_late()
/*
struct Q { int q; };  struct P : Q { int p; };
struct E : P { virtual void f(); };
E is dynamic: its layout facts come right away.  P is plain when E is laid
out and later gets CLASS_MANGLE, CLASS_INFO and CLASS_BASES (a private base,
so the stream's flag differs from the plain default).  The decoder must use
the late facts, and set_plain_class only for Q, which never gets any.
*/
{
  std::string text = std::string(header) +
    "frame DECLS\n"
    "  TY_INT 1 32 true 4 4\n"
    "  TY_VPTR 3\n"
    "  STR 2 \"1E\"\n"
    "  STR 3 \"1P\"\n"
    "  TY_RECORD_DECL 30 0 nil\n"
    "  TY_RECORD_BODY 30 4 4 4 0 1\n"
    "  REC_FIELD 1 0 nil nil nil 0\n"
    "  TY_RECORD_DECL 31 0 nil\n"
    "  TY_RECORD_BODY 31 8 8 4 0 2\n"
    "  REC_FIELD 30 0 nil nil nil 16\n"
    "  REC_FIELD 1 4 nil nil nil 0\n"
    "  TY_RECORD_DECL 32 0 nil\n"
    "  TY_RECORD_BODY 32 16 16 8 0 2\n"
    "  REC_FIELD 3 0 nil nil nil 1\n"
    "  REC_FIELD 31 8 nil nil nil 16\n"
    "  CLASS_MANGLE 32 2\n"
    "  CLASS_INFO 32 16 8 nil 1\n"
    "  CLASS_BASES 32 [1, 1]\n"
    "  TY_VOID 2\n"
    "  STR 4 \"_ZN1E1fEv\"\n"
    "  TY_PTR 33 32\n"
    "  TY_FUNC 34 2 0 [33]\n"
    "  DECL_FUNC 40 34 0 1 4\n"
    "  CLASS_VFUNCS 32 [40, 0, 0, 1]\n"
    "  CLASS_OVERRIDES 32 []\n"
    "  CLASS_MANGLE 31 3\n"
    "  CLASS_INFO 31 8 4 nil 0\n"
    "  CLASS_BASES 31 [0, 0]\n"
    "frame END\n"
    "  END 2 0\n";
  edg_abi::a_class_db db;
  std::string problem;
  bool ok = decode(text, db, problem);

  check(ok, "plain class with late facts: " + problem);
  if (ok) {
    edg_abi::a_class_id e = find(db, 32);
    edg_abi::a_class_id p = find(db, 31);
    edg_abi::a_class_id q = find(db, 30);

    check(e != edg_abi::no_id && p != edg_abi::no_id && q != edg_abi::no_id,
          "plain class with late facts: E, P and Q are defined");
    if (p != edg_abi::no_id) {
      check(db.classes[p].name == "P", "late CLASS_MANGLE gives the name");
      check(db.classes[p].bases.size() == 1 &&
                !db.classes[p].bases[0].is_public,
            "the late CLASS_BASES flags are used (private base)");
    }
    if (e != edg_abi::no_id)
      check(db.classes[e].bases.size() == 1 &&
                db.classes[e].bases[0].is_public,
            "E's CLASS_BASES flags");
    if (q != edg_abi::no_id)
      check(db.classes[q].bases.empty() && !db.classes[q].is_dynamic,
            "Q stays plain");
  }
}  /* test_plain_class_gets_info_late */

void test_plain_class_never_gets_info()
/*
The same hierarchy without the late facts: P and Q go through
set_plain_class and the BASE fields (the base is public by default).
*/
{
  std::string text = std::string(header) +
    "frame DECLS\n"
    "  TY_INT 1 32 true 4 4\n"
    "  TY_VPTR 3\n"
    "  STR 2 \"1E\"\n"
    "  TY_RECORD_DECL 30 0 nil\n"
    "  TY_RECORD_BODY 30 4 4 4 0 1\n"
    "  REC_FIELD 1 0 nil nil nil 0\n"
    "  TY_RECORD_DECL 31 0 nil\n"
    "  TY_RECORD_BODY 31 8 8 4 0 2\n"
    "  REC_FIELD 30 0 nil nil nil 16\n"
    "  REC_FIELD 1 4 nil nil nil 0\n"
    "  TY_RECORD_DECL 32 0 nil\n"
    "  TY_RECORD_BODY 32 16 16 8 0 2\n"
    "  REC_FIELD 3 0 nil nil nil 1\n"
    "  REC_FIELD 31 8 nil nil nil 16\n"
    "  CLASS_MANGLE 32 2\n"
    "  CLASS_INFO 32 16 8 nil 1\n"
    "  CLASS_BASES 32 [1, 1]\n"
    "  TY_VOID 2\n"
    "  STR 4 \"_ZN1E1fEv\"\n"
    "  TY_PTR 33 32\n"
    "  TY_FUNC 34 2 0 [33]\n"
    "  DECL_FUNC 40 34 0 1 4\n"
    "  CLASS_VFUNCS 32 [40, 0, 0, 1]\n"
    "  CLASS_OVERRIDES 32 []\n"
    "frame END\n"
    "  END 2 0\n";
  edg_abi::a_class_db db;
  std::string problem;
  bool ok = decode(text, db, problem);

  check(ok, "plain classes only: " + problem);
  if (ok) {
    edg_abi::a_class_id p = find(db, 31);

    check(db.classes.size() == 3, "three classes reach the builder");
    if (p != edg_abi::no_id)
      check(db.classes[p].bases.size() == 1 &&
                db.classes[p].bases[0].is_public &&
                !db.classes[p].is_dynamic,
            "P: its BASE field is its base, public by default");
  }
}  /* test_plain_class_never_gets_info */

void test_no_unique_address_member_is_not_data()
/*
A [[no_unique_address]] member of an empty class (POTENTIALLY_OVERLAPPING and
EMPTY_FOR_ABI) is zero-size and does not make its class non-empty (clang's
isZeroSize), so a base of that class is empty for the layout and the RTTI;
a [[no_unique_address]] member of a class with data does.
*/
{
  std::string text = std::string(header) +
    "frame DECLS\n"
    "  TY_INT 1 32 true 4 4\n"
    "  TY_VPTR 3\n"
    "  STR 2 \"1Q\"\n"
    "  TY_RECORD_DECL 30 0 nil\n"
    "  TY_RECORD_BODY 30 1 0 1 0 0\n"
    "  TY_RECORD_DECL 31 0 nil\n"
    "  TY_RECORD_BODY 31 1 0 1 0 1\n"
    "  REC_FIELD 30 0 nil nil nil 34\n"
    "  TY_RECORD_DECL 34 0 nil\n"
    "  TY_RECORD_BODY 34 4 4 4 0 1\n"
    "  REC_FIELD 1 0 nil nil nil 0\n"
    "  TY_RECORD_DECL 35 0 nil\n"
    "  TY_RECORD_BODY 35 4 4 4 0 1\n"
    "  REC_FIELD 34 0 nil nil nil 32\n"
    "  TY_RECORD_DECL 32 0 nil\n"
    "  TY_RECORD_BODY 32 16 16 8 0 3\n"
    "  REC_FIELD 3 0 nil nil nil 1\n"
    "  REC_FIELD 31 8 nil nil nil 18\n"
    "  REC_FIELD 35 8 nil nil nil 16\n"
    "  CLASS_MANGLE 32 2\n"
    "  CLASS_INFO 32 16 8 nil 1\n"
    "  CLASS_BASES 32 [1, 1, 2, 1]\n"
    "  TY_VOID 2\n"
    "  STR 4 \"_ZN1Q1fEv\"\n"
    "  TY_PTR 33 32\n"
    "  TY_FUNC 36 2 0 [33]\n"
    "  DECL_FUNC 40 36 0 1 4\n"
    "  CLASS_VFUNCS 32 [40, 0, 0, 1]\n"
    "  CLASS_OVERRIDES 32 []\n"
    "frame END\n"
    "  END 2 0\n";
  edg_abi::a_class_db db;
  std::string problem;
  bool ok = decode(text, db, problem);

  check(ok, "no_unique_address: " + problem);
  if (ok) {
    edg_abi::a_class_id empty_holder = find(db, 31);
    edg_abi::a_class_id data_holder = find(db, 35);

    check(empty_holder != edg_abi::no_id && db.classes[empty_holder].is_empty,
          "a class whose only member is an empty [[no_unique_address]] "
          "member is empty");
    check(data_holder != edg_abi::no_id && !db.classes[data_holder].is_empty,
          "a [[no_unique_address]] member of a class with data is data");
  }
}  /* test_no_unique_address_member_is_not_data */

}  /* anonymous namespace */

int main()
/*
Run the tests; 0 when all pass.
*/
{
  test_plain_class_gets_info_late();
  test_plain_class_never_gets_info();
  test_no_unique_address_member_is_not_data();
  std::printf("abi_decode_tests: %s (%d failures)\n",
              failures == 0 ? "OK" : "FAILED", failures);
  return failures == 0 ? 0 : 1;
}  /* main */
