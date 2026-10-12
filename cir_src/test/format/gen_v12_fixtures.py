#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Generates the bad_v12_* format fixtures and good_v12_function_try.txt from
# the three hand-written good streams (good_v12_classes.txt,
# good_v12_exceptions.txt, good_v12_member_pointers.txt) by one mutation each:
#
#   python3 cir_src/test/format/gen_v12_fixtures.py
#
# Edit the mutations here, not the generated files.
import os, sys
OUT=os.path.join(os.path.dirname(os.path.abspath(__file__)), 'fixtures')
base = {}
for n, f in (('classes','good_v12_classes'),('exceptions','good_v12_exceptions'),('memptr','good_v12_member_pointers')):
  txt=open(f'{OUT}/{f}.txt').read()
  lines=txt.split('\n')
  assert lines[0].startswith(';')
  base[n]=(lines[0], '\n'.join(lines[1:]))

made=[]
def emit(name, kind, expect, text, comment=None):
  hdr=f"; expect: {kind} {expect}\n"
  if comment: hdr += f"; {comment}\n"
  open(f'{OUT}/{name}.txt','w').write(hdr+text)
  made.append(name)

def mut(b, pairs):
  t=base[b][1]
  for old,new in pairs:
    assert old in t, (old, b)
    t=t.replace(old,new,1)
  return t

def bad(name, b, pairs, expect, kind='format', comment=None):
  emit(name, kind, expect, mut(b,pairs), comment)

# --- small helpers for the exception and class streams
EH_PRE = """ecil 1.2 features=0x00000000
frame HEADER
  STR 1 "x86_64-unknown-linux-gnu"
  HDR_TARGET 1 nil nil 64 64 6
  HDR_LANG 1 201703 %s
frame REGION
  TY_INT 1 32 true 4 4
  TY_VOID 2
  STR 2 "_ZTIi"
  STR 3 "f"
  STR 4 "n"
  TY_FUNC 3 2 0 []
  DECL_ABI_DATA 10 2 1 0 0 2
  TYPEINFO_DEF 10 0 0 nil nil
  DECL_FUNC 20 3 0 1 3
  DECL_FUNC 21 3 0 129 4
"""
def eh(name, body, expect, flags=6, kind='format', comment=None, decl='', fn=20):
  txt = (EH_PRE % flags) + decl + "  FUNC_BEGIN %d 0\n" % fn + body + "  FUNC_END 1\nframe END\n  END 2 0\n"
  emit(name, kind, expect, txt, comment)

# TRY / CATCH shapes
eh('bad_v12_try_without_catch', "  TRY_BEGIN 0\n  BLOCK_BEGIN\n  BLOCK_END\n  TRY_END\n", "TRY_END without TRY_BEGIN, or a TRY without a CATCH_BEGIN")
eh('bad_v12_catch_without_try', "  CATCH_BEGIN nil\n", "CATCH_BEGIN without TRY_BEGIN, or after catch (...)")
eh('bad_v12_catch_after_catch_all', "  TRY_BEGIN 0\n  CATCH_BEGIN nil\n  CATCH_BEGIN 10\n  TRY_END\n", "CATCH_BEGIN without TRY_BEGIN, or after catch (...)")
eh('bad_v12_try_end_without_try', "  TRY_END\n", "TRY_END without TRY_BEGIN")
eh('bad_v12_try_unclosed', "  TRY_BEGIN 0\n  CATCH_BEGIN nil\n", "unclosed bracket at FUNC_END")
eh('bad_v12_catch_param_without_local', "  TRY_BEGIN 0\n  CATCH_BEGIN 10\n  CATCH_PARAM 1 2 nil\n  TRY_END\n", "CATCH_PARAM must directly follow the LOCAL of its parameter")
eh('bad_v12_catch_param_other_local', "  LOCAL 1 1 nil 4 0\n  TRY_BEGIN 0\n  CATCH_BEGIN 10\n  LOCAL 2 1 nil 4 0\n  CATCH_PARAM 1 2 nil\n  TRY_END\n", "CATCH_PARAM must directly follow the LOCAL of its parameter")
eh('bad_v12_catch_param_late', "  TRY_BEGIN 0\n  CATCH_BEGIN 10\n  LOCAL 1 1 nil 4 0\n  CONST_INT 1 1\n  DROP\n  CATCH_PARAM 1 2 nil\n  TRY_END\n", "CATCH_PARAM must directly follow the LOCAL of its parameter")
eh('bad_v12_catch_param_kind3', "  TRY_BEGIN 0\n  CATCH_BEGIN 10\n  LOCAL 1 1 nil 4 0\n  CATCH_PARAM 1 3 nil\n  TRY_END\n", "CATCH_PARAM kind 3 is invalid")
eh('bad_v12_catch_param_copy_of_int', "  TRY_BEGIN 0\n  CATCH_BEGIN 10\n  LOCAL 1 1 nil 4 0\n  CATCH_PARAM 1 4 nil\n  TRY_END\n", "CATCH_PARAM copy kinds need a parameter of a record type")
eh('bad_v12_catch_param_ref_to_ptr_of_int', "  TRY_BEGIN 0\n  CATCH_BEGIN 10\n  LOCAL 1 1 nil 4 0\n  CATCH_PARAM 1 6 nil\n  TRY_END\n", "reference to a pointer")
eh('bad_v12_catch_param_copy_fn_on_scalar', "  TRY_BEGIN 0\n  CATCH_BEGIN 10\n  LOCAL 1 1 nil 4 0\n  CATCH_PARAM 1 2 21\n  TRY_END\n", "CATCH_PARAM g_copy is only for non_trivial_copy")
eh('bad_v12_catch_typeinfo_not_abi', "  TRY_BEGIN 0\n  CATCH_BEGIN 20\n  TRY_END\n", "is not a DECL_ABI_DATA typeinfo")
eh('bad_v12_try_without_exceptions', "  TRY_BEGIN 0\n  CATCH_BEGIN nil\n  TRY_END\n", "TRY_BEGIN needs exceptions = 1 in HDR_LANG", flags=4)
eh('bad_v12_throw_without_exceptions', "  RETHROW\n", "RETHROW needs exceptions = 1 in HDR_LANG", flags=4)
eh('bad_v12_try_in_fullexpr', "  FULLEXPR_BEGIN\n  TRY_BEGIN 0\n", "TRY_BEGIN is not allowed inside a FULLEXPR")
# THROW
eh('bad_v12_throw_end_without_begin', "  THROW_END 10 nil\n", "THROW_END without THROW_BEGIN")
eh('bad_v12_throw_unclosed', "  THROW_BEGIN 1\n", "THROW_BEGIN without THROW_END at FUNC_END")
eh('bad_v12_throw_in_cleanup_code', "  CLEANUP_BEGIN 2 nil 0\n  THROW_BEGIN 1\n", "THROW_BEGIN is not allowed inside cleanup code")
eh('bad_v12_rethrow_in_cleanup_code', "  CLEANUP_BEGIN 2 nil 0\n  RETHROW\n", "RETHROW is not allowed inside cleanup code")
eh('bad_v12_throw_end_active_cleanup', "  FULLEXPR_BEGIN\n  THROW_BEGIN 1\n  CLEANUP_BEGIN 2 nil 1\n  CLEANUP_BODY\n  THROW_END 10 nil\n", "still active at THROW_END")
eh('bad_v12_throw_end_other_bracket', "  THROW_BEGIN 1\n  BLOCK_BEGIN\n  THROW_END 10 nil\n", "THROW_END is in another bracket than its THROW_BEGIN")
eh('bad_v12_throw_end_typeinfo', "  THROW_BEGIN 1\n  THROW_END 20 nil\n", "is not a DECL_ABI_DATA typeinfo")
# CLEANUP_END
eh('bad_v12_cleanup_end_none', "  FULLEXPR_BEGIN\n  CLEANUP_END\n", "CLEANUP_END without an active DEACTIVATABLE cleanup")
eh('bad_v12_cleanup_end_twice', "  FULLEXPR_BEGIN\n  CLEANUP_BEGIN 2 nil 1\n  CLEANUP_BODY\n  CLEANUP_END\n  CLEANUP_END\n", "CLEANUP_END without an active DEACTIVATABLE cleanup")
eh('bad_v12_cleanup_end_not_deactivatable', "  FULLEXPR_BEGIN\n  CLEANUP_BEGIN 2 nil 0\n  CLEANUP_BODY\n  CLEANUP_END\n", "CLEANUP_END without an active DEACTIVATABLE cleanup")
eh('bad_v12_cleanup_end_other_arm', "  FULLEXPR_BEGIN\n  CLEANUP_BEGIN 2 nil 1\n  CLEANUP_BODY\n  CONST_INT 1 1\n  TO_BOOL\n  TERN_BEGIN 1\n  CLEANUP_END\n", "CLEANUP_END is in another FULLEXPR or conditional arm")
eh('bad_v12_cleanup_end_other_fullexpr', "  CLEANUP_BEGIN 2 nil 1\n  CLEANUP_BODY\n  FULLEXPR_BEGIN\n  CLEANUP_END\n", "CLEANUP_END is in another FULLEXPR or conditional arm")
eh('bad_v12_cleanup_end_region_closed', "  BLOCK_BEGIN\n  CLEANUP_BEGIN 2 nil 1\n  CLEANUP_BODY\n  BLOCK_END\n  CLEANUP_END\n", "a DEACTIVATABLE cleanup is still active at the end of its range")
eh('bad_v12_deactivatable_unended', "  FULLEXPR_BEGIN\n  CLEANUP_BEGIN 2 nil 1\n  CLEANUP_BODY\n  FULLEXPR_END\n", "a DEACTIVATABLE cleanup is still active at the end of its range")
eh('bad_v12_deactivatable_unended_function', "  CLEANUP_BEGIN 3 nil 1\n  CLEANUP_BODY\n", "a DEACTIVATABLE cleanup is still active at the end of its range")
eh('bad_v12_throw_conditional_without_fullexpr', "  CONST_INT 1 1\n  TO_BOOL\n  TERN_BEGIN 1\n  THROW_BEGIN 1\n", "THROW_BEGIN in a conditional region needs an enclosing FULLEXPR")
eh('bad_v12_cleanup_end_in_cleanup_code', "  CLEANUP_BEGIN 2 nil 1\n  CLEANUP_BODY\n  CLEANUP_BEGIN 2 nil 0\n  CLEANUP_END\n", "CLEANUP_END is not allowed inside cleanup code")
eh('bad_v12_cleanup_deactivatable_normal', "  CLEANUP_BEGIN 1 nil 1\n", "DEACTIVATABLE is only for eh and all cleanups")
eh('bad_v12_cleanup_deactivatable_flag_bits', "  CLEANUP_BEGIN 2 nil 2\n", "CLEANUP_BEGIN operand 3: unknown flag bits 0x2")
# FUNC_EH_SPEC and function-try
eh('bad_v12_eh_spec_late', "  CONST_INT 1 1\n  DROP\n  FUNC_EH_SPEC 1 []\n", "FUNC_EH_SPEC is a header record")
eh('bad_v12_eh_spec_twice', "  FUNC_EH_SPEC 1 []\n  FUNC_EH_SPEC 1 []\n", "FUNC_EH_SPEC appears twice in a function", fn=21)
eh('bad_v12_eh_spec_noexcept_without_nothrow', "  FUNC_EH_SPEC 1 []\n", "FUNC_EH_SPEC noexcept in a function whose DECL_FUNC has no NOTHROW")
eh('bad_v12_eh_spec_without_exceptions', "  FUNC_EH_SPEC 2 []\n", "FUNC_EH_SPEC needs exceptions = 1 in HDR_LANG", flags=4)
eh('bad_v12_eh_spec_noexcept_list', "  FUNC_EH_SPEC 1 [10]\n", "FUNC_EH_SPEC noexcept has no type list", fn=21)
eh('bad_v12_eh_spec_dynamic_type', "  FUNC_EH_SPEC 2 [20]\n", "is not a DECL_ABI_DATA typeinfo")
eh('bad_v12_function_try_in_function', "  TRY_BEGIN 1\n", "TRY_BEGIN FUNCTION_TRY is only for the body of a constructor or destructor")
eh('bad_v12_try_flag_bits', "  TRY_BEGIN 2\n", "TRY_BEGIN operand 1: unknown flag bits 0x2")
eh('bad_v12_array_partial_dtor_without_exceptions', "  ARRAY_PARTIAL_DTOR 20\n", "ARRAY_PARTIAL_DTOR needs exceptions = 1 in HDR_LANG", flags=4)
eh('bad_v12_array_partial_dtor_not_function', "  ARRAY_PARTIAL_DTOR 10\n", "ARRAY_PARTIAL_DTOR names a G-id that is not a declared function")
eh('bad_v12_typeid_no_rtti', "  TYPEID 10 1 0\n", "TYPEID needs rtti = 1 in HDR_LANG", flags=2)
eh('bad_v12_dynamic_cast_non_class_typeinfo', "  CONST_NULL 1\n  DYNAMIC_CAST 10 nil 1 0\n", "is not the typeinfo of a class")

# --- the exception stream: ABI data of a type that is not a class
bad('bad_v12_abi_nonclass_without_name','exceptions',[("DECL_ABI_DATA 10 2 1 0 0 2","DECL_ABI_DATA 10 2 1 0 0 nil")],"needs its name (sid)")
bad('bad_v12_abi_nonclass_without_def','exceptions',[("  TYPEINFO_DEF 10 0 0 nil nil\n","")],"has no TYPEINFO_DEF")
bad('bad_v12_typeinfo_def_twice','exceptions',[("  TYPEINFO_DEF 10 0 0 nil nil\n","  TYPEINFO_DEF 10 0 0 nil nil\n  TYPEINFO_DEF 10 0 0 nil nil\n")],"TYPEINFO_DEF of G-id 10 appears twice")
bad('bad_v12_fundamental_defined_here','exceptions',[("DECL_ABI_DATA 10 2 1 0 0 2","DECL_ABI_DATA 10 2 1 0 1 2")],"a fundamental typeinfo is an external declaration")
bad('bad_v12_typeinfo_def_flags','exceptions',[("TYPEINFO_DEF 10 0 0 nil nil","TYPEINFO_DEF 10 0 1 nil nil")],"TYPEINFO_DEF flags are for pointer and member_pointer only")
bad('bad_v12_typeinfo_def_pointer_without_pointee','exceptions',[("TYPEINFO_DEF 10 0 0 nil nil","TYPEINFO_DEF 10 1 0 nil nil")],"missing typeinfo G-id")
bad('bad_v12_typeinfo_def_flag_bits','exceptions',[("TYPEINFO_DEF 10 0 0 nil nil","TYPEINFO_DEF 10 1 128 10 nil")],"TYPEINFO_DEF operand 3: unknown flag bits 0x80")
bad('bad_v12_abi_kind_range','exceptions',[("DECL_ABI_DATA 10 2 1 0 0 2","DECL_ABI_DATA 10 3 1 0 0 2")],"DECL_ABI_DATA operand 2: value 3 out of range")

# --- the class stream
bad('bad_v12_class_info_twice','classes',[("  CLASS_MANGLE 100 11\n","  CLASS_MANGLE 100 11\n  CLASS_INFO 100 4 4 nil 0\n  CLASS_INFO 100 4 4 nil 0\n")],"CLASS_INFO of G-id 100 appears twice")
bad('bad_v12_class_info_nvsize_over_size','classes',[("CLASS_INFO 10 12 8 nil 3","CLASS_INFO 10 20 8 nil 3")],"is above the size 16")
open(f'{OUT}/good_v12_nvsize_above_data_size.txt','w').write("; ECIL 1.2: without virtual bases data_size <= nvsize <= size (a displaced empty base makes nvsize larger than data_size).\n"+mut('classes',[("CLASS_INFO 10 12 8 nil 3","CLASS_INFO 10 16 8 nil 3")]))
bad('bad_v12_class_info_nvalign','classes',[("CLASS_INFO 10 12 8 nil 3","CLASS_INFO 10 12 3 nil 3")],"is not a power of two within the alignment 8")
bad('bad_v12_class_info_primary_not_base','classes',[("CLASS_INFO 20 16 8 0 3","CLASS_INFO 20 16 8 1 3")],"is not a BASE or VIRTUAL_BASE field at offset 0")
bad('bad_v12_class_info_primary_out_of_range','classes',[("CLASS_INFO 20 16 8 0 3","CLASS_INFO 20 16 8 9 3")],"is not a BASE or VIRTUAL_BASE field at offset 0")
bad('bad_v12_class_info_nslots_below_primary','classes',[("CLASS_INFO 20 16 8 0 3","CLASS_INFO 20 16 8 0 2")],"is below that of its primary base")
bad('bad_v12_class_info_without_mangle','classes',[("  CLASS_MANGLE 10 9\n","")],"needs its CLASS_MANGLE first")
bad('bad_v12_class_info_nslots_not_dynamic','classes',[("  CLASS_MANGLE 100 11\n","  CLASS_MANGLE 100 11\n  CLASS_INFO 100 4 4 nil 1\n")],"a class without a vptr")
bad('bad_v12_class_info_nvsize_below_fields','classes',[("CLASS_INFO 110 12 8 nil 0","CLASS_INFO 110 8 8 nil 0")],"below the end of its last field that is not a virtual base")
bad('bad_v12_class_mangle_not_record','classes',[("  CLASS_MANGLE 100 11\n","  CLASS_MANGLE 1 9\n  CLASS_MANGLE 100 11\n")],"which is not a record")
bad('bad_v12_class_mangle_empty','classes',[("  STR 37 \"l\"\n","  STR 37 \"l\"\n  STR 38 \"\"\n"),("  CLASS_MANGLE 10 9\n","  CLASS_MANGLE 10 38\n")],"CLASS_MANGLE: an empty mangling")
bad('bad_v12_class_bases_omits_base','classes',[("CLASS_BASES 20 [0, 1]","CLASS_BASES 20 []")],"omits a BASE field")
bad('bad_v12_class_bases_flag_bits','classes',[("CLASS_BASES 20 [0, 1]","CLASS_BASES 20 [0, 2]")],"CLASS_BASES: unknown flag bits")
bad('bad_v12_class_bases_not_a_base','classes',[("CLASS_BASES 20 [0, 1]","CLASS_BASES 20 [1, 1]")],"is not a BASE or VIRTUAL_BASE field of G-id 20")
bad('bad_v12_class_bases_twice_listed','classes',[("CLASS_BASES 20 [0, 1]","CLASS_BASES 20 [0, 1, 0, 1]")],"names field 0 twice")
bad('bad_v12_class_bases_odd_array','classes',[("CLASS_BASES 20 [0, 1]","CLASS_BASES 20 [0, 1, 1]")],"CLASS_BASES array length is not a multiple of 2")
bad('bad_v12_class_bases_vbase_set','classes',[("CLASS_BASES 110 [2, 1]","CLASS_BASES 110 []")],"are not the direct virtual bases plus the virtual bases of the direct bases")
bad('bad_v12_class_bases_before_info','classes',[("  CLASS_MANGLE 100 11\n","  CLASS_MANGLE 100 11\n  CLASS_BASES 100 []\n  CLASS_INFO 100 4 4 nil 0\n")],"must follow its CLASS_INFO, once")
bad('bad_v12_class_vfuncs_not_dynamic','classes',[("  CLASS_MANGLE 100 11\n","  CLASS_MANGLE 100 11\n  CLASS_INFO 100 4 4 nil 0\n  CLASS_BASES 100 []\n  CLASS_VFUNCS 100 []\n")],"a class without a vptr")
bad('bad_v12_class_vfuncs_slot_range','classes',[("[30, 0, 0, 0, 33, 2, 0, 1]","[30, 0, 0, 0, 33, 5, 0, 1]")],"is outside the 3 slots of the class")
bad('bad_v12_class_vfuncs_dtor_slot_pair','classes',[("[30, 0, 0, 0, 33, 2, 0, 1]","[30, 2, 0, 0, 33, 1, 0, 1]")],"is outside the 3 slots of the class")
bad('bad_v12_class_vfuncs_dtor_sig','classes',[("[30, 0, 0, 0, 33, 2, 0, 1]","[30, 0, 0, 7, 33, 2, 0, 1]")],"sig is 0 exactly for destructors")
bad('bad_v12_class_vfuncs_method_sig_zero','classes',[("[30, 0, 0, 0, 33, 2, 0, 1]","[30, 0, 0, 0, 33, 2, 0, 0]")],"sig is 0 exactly for destructors")
bad('bad_v12_class_vfuncs_duplicate','classes',[("[42, 2, 0, 1, 43, 0, 4, 0]","[42, 2, 0, 1, 42, 2, 0, 1]")],"is listed by two CLASS_VFUNCS entries")
bad('bad_v12_class_vfuncs_other_class','classes',[("[30, 0, 0, 0, 33, 2, 0, 1]","[30, 0, 0, 0, 42, 2, 0, 1]")],"is not a declared member function of G-id 10")
bad('bad_v12_class_vfuncs_base_variant_dtor','classes',[("[30, 0, 0, 0, 33, 2, 0, 1]","[31, 0, 0, 0, 33, 2, 0, 1]")],"lists a destructor as its D1")
bad('bad_v12_class_vfuncs_flag_bits','classes',[("[30, 0, 0, 0, 33, 2, 0, 1]","[30, 0, 8, 0, 33, 2, 0, 1]")],"CLASS_VFUNCS: unknown flag bits")
bad('bad_v12_class_vfuncs_odd_array','classes',[("[30, 0, 0, 0, 33, 2, 0, 1]","[30, 0, 0, 0, 33, 2, 0]")],"CLASS_VFUNCS array length is not a multiple of 4")
bad('bad_v12_class_overrides_unlisted','classes',[("[33, 0, 42, 0, 30, 0, 43, 0]","[33, 0, 70, 0, 30, 0, 43, 0]")],"names a function that no CLASS_VFUNCS lists")
bad('bad_v12_class_overrides_self','classes',[("[33, 0, 42, 0, 30, 0, 43, 0]","[33, 0, 33, 0, 30, 0, 43, 0]")],"lists only entries with g_ovr != g_fn")
bad('bad_v12_class_overrides_duplicate','classes',[("[33, 0, 42, 0, 30, 0, 43, 0]","[33, 0, 42, 0, 33, 0, 42, 0]")],"has two final overriders for one function at one subobject")
bad('bad_v12_class_overrides_offset','classes',[("[33, 0, 42, 0, 30, 0, 43, 0]","[33, 99, 42, 0, 30, 0, 43, 0]")],"CLASS_OVERRIDES offset beyond the size of G-id 20")
bad('bad_v12_class_overrides_before_vfuncs','classes',[("  CLASS_VFUNCS 10 [30, 0, 0, 0, 33, 2, 0, 1]\n  CLASS_OVERRIDES 10 []\n","  CLASS_OVERRIDES 10 []\n  CLASS_VFUNCS 10 [30, 0, 0, 0, 33, 2, 0, 1]\n")],"must follow its CLASS_VFUNCS, once, and is for a dynamic class")
bad('bad_v12_first_need_base_facts','classes',[("  CLASS_OVERRIDES 10 []\n","")],"must come before CLASS_VFUNCS of a class derived from it")
bad('bad_v12_first_need_abi_data','classes',[("  CLASS_OVERRIDES 20 [33, 0, 42, 0, 30, 0, 43, 0]\n","")],"must come before DECL_ABI_DATA of the class")
bad('bad_v12_vptr_not_first','classes',[("  REC_FIELD 3 0 nil nil nil 1\n  REC_FIELD 1 8 nil nil 7 0\n  CLASS_MANGLE 10 9","  REC_FIELD 1 0 nil nil 7 0\n  REC_FIELD 3 8 nil nil nil 1\n  CLASS_MANGLE 10 9")],"a TY_VPTR field is the unnamed field 0 at offset 0")
bad('bad_v12_vptr_named','classes',[("  REC_FIELD 3 0 nil nil nil 1\n  REC_FIELD 1 8 nil nil 7 0\n  CLASS_MANGLE 10 9","  REC_FIELD 3 0 nil nil 7 0\n  REC_FIELD 1 8 nil nil 7 0\n  CLASS_MANGLE 10 9")],"a TY_VPTR field is the unnamed field 0 at offset 0")
bad('bad_v12_vptr_as_type','classes',[("  DECL_FUNC 70 130 0 1 35\n","  DECL_FUNC 70 130 0 1 35\n  DECL_VAR 200 3 0 1 8 nil\n")],"is only the type of the vptr field of a record")
bad('bad_v12_vptr_without_class_info','classes',[("  TY_RECORD_DECL 20 0 3\n","  TY_RECORD_DECL 8 0 3\n  TY_RECORD_BODY 8 8 8 8 0 1\n  REC_FIELD 3 0 nil nil nil 1\n  TY_RECORD_DECL 20 0 3\n")],"follow its TY_RECORD_BODY directly")
bad('bad_v12_layout_facts_not_direct','classes',[("  REC_FIELD 1 8 nil nil 7 0\n  CLASS_MANGLE 10 9\n","  REC_FIELD 1 8 nil nil 7 0\n  TY_PTR 300 10\n  CLASS_MANGLE 10 9\n")],"follow its TY_RECORD_BODY directly")
bad('bad_v12_layout_facts_other_class','classes',[("  REC_FIELD 1 8 nil nil 7 0\n  CLASS_MANGLE 10 9\n","  REC_FIELD 1 8 nil nil 7 0\n  CLASS_MANGLE 1 9\n  CLASS_MANGLE 10 9\n")],"follow its TY_RECORD_BODY directly")
bad('bad_v12_layout_facts_order','classes',[("  CLASS_MANGLE 10 9\n  CLASS_INFO 10 12 8 nil 3\n  CLASS_BASES 10 []\n","  CLASS_MANGLE 10 9\n  CLASS_BASES 10 []\n  CLASS_INFO 10 12 8 nil 3\n")],"follow its TY_RECORD_BODY directly")
open(f'{OUT}/good_v12_layout_facts_after_string.txt','w').write("; ECIL 1.2: a string may sit between a record body and its layout facts.\n"+mut('classes',[("  REC_FIELD 1 8 nil nil 7 0\n  CLASS_MANGLE 10 9\n","  REC_FIELD 1 8 nil nil 7 0\n  STR 40 \"zz\"\n  CLASS_MANGLE 10 9\n")]))
emit('bad_v12_layout_facts_frame_end','format',"are missing at the end of its frame","""ecil 1.2 features=0x00000000
frame HEADER
  STR 1 "x86_64-unknown-linux-gnu"
  HDR_TARGET 1 nil nil 64 64 6
  HDR_LANG 1 201703 0
frame DECLS
  TY_INT 1 32 true 4 4
  TY_VPTR 3
  TY_RECORD_DECL 10 0 nil
  TY_RECORD_BODY 10 16 12 8 0 2
  REC_FIELD 3 0 nil nil nil 1
  REC_FIELD 1 8 nil nil nil 0
frame END
  END 2 0
""")
bad('bad_v12_vbase_before_data','classes',[("  REC_FIELD 1 8 nil nil 7 0\n  REC_FIELD 100 12 nil nil nil 64\n  CLASS_MANGLE 110 12","  REC_FIELD 100 12 nil nil nil 64\n  REC_FIELD 1 8 nil nil 7 0\n  CLASS_MANGLE 110 12")],"VIRTUAL_BASE fields come after the data members")
bad('bad_v12_vbase_duplicate','classes',[("  TY_RECORD_BODY 120 16 16 8 0 2\n  REC_FIELD 110 0 nil nil nil 16\n  REC_FIELD 100 12 nil nil nil 64\n","  TY_RECORD_BODY 120 16 16 8 0 3\n  REC_FIELD 110 0 nil nil nil 16\n  REC_FIELD 100 12 nil nil nil 64\n  REC_FIELD 100 12 nil nil nil 64\n")],"has two VIRTUAL_BASE fields of one class")
bad('bad_v12_vbase_flag_combination','classes',[("REC_FIELD 100 12 nil nil nil 64\n  CLASS_MANGLE 110 12","REC_FIELD 100 12 nil nil nil 80\n  CLASS_MANGLE 110 12")],"a VIRTUAL_BASE field is a record-typed field that is not BASE")
bad('bad_v12_vbase_of_non_record','classes',[("REC_FIELD 100 12 nil nil nil 64\n  CLASS_MANGLE 110 12","REC_FIELD 1 12 nil nil nil 64\n  CLASS_MANGLE 110 12")],"a VIRTUAL_BASE field is a record-typed field")
bad('bad_v12_vbase_data_size','classes',[("TY_RECORD_BODY 110 16 16 8 0 3","TY_RECORD_BODY 110 16 12 8 0 3")],"is below the end of its last field that is not EMPTY_FOR_ABI")
bad('bad_v12_structor_has_vbases_mismatch','classes',[("STRUCTOR 52 53 2","STRUCTOR 52 53 0")],"STRUCTOR has_vbases does not match the VIRTUAL_BASE fields of the class")
bad('bad_v12_structor_has_vbases_missing','classes',[("STRUCTOR 40 41 0","STRUCTOR 40 41 2")],"STRUCTOR has_vbases does not match the VIRTUAL_BASE fields of the class")
bad('bad_v12_structor_deleting_twice','classes',[("  STRUCTOR_DELETING 32 30 36 0\n","  STRUCTOR_DELETING 32 30 36 0\n  STRUCTOR_DELETING 32 30 36 0\n")],"STRUCTOR_DELETING of G-id 30 appears twice")
bad('bad_v12_structor_deleting_d0','classes',[("STRUCTOR_DELETING 32 30 36 0","STRUCTOR_DELETING 31 30 36 0")],"g_d0 is its own DECL_FUNC with the type and linkage of D1")
bad('bad_v12_structor_deleting_not_dtor','classes',[("STRUCTOR_DELETING 32 30 36 0","STRUCTOR_DELETING 32 34 36 0")],"g_d1 is not the complete variant of a destructor STRUCTOR")
bad('bad_v12_structor_deleting_d0_defined','classes',[("  DECL_FUNC 45 22 0 1 26\n","  DECL_FUNC 45 22 0 0 26\n")],"is defined here if and only if its D1")
bad('bad_v12_structor_deleting_flags','classes',[("STRUCTOR_DELETING 32 30 36 0","STRUCTOR_DELETING 32 30 36 1")],"flags SIZED/ALIGNED do not match the parameters of g_delete")
bad('bad_v12_structor_deleting_delete','classes',[("STRUCTOR_DELETING 32 30 36 0","STRUCTOR_DELETING 32 30 10 0")],"g_delete is not a declared function")
bad('bad_v12_abi_vtable_not_dynamic','classes',[("  CLASS_MANGLE 100 11\n","  CLASS_MANGLE 100 11\n  DECL_ABI_DATA 67 0 100 0 1 nil\n")],"which is not a dynamic class")
bad('bad_v12_abi_vtt_no_vbases','classes',[("DECL_ABI_DATA 63 1 110 0 1 nil","DECL_ABI_DATA 63 1 10 0 1 nil")],"which has no virtual bases")
bad('bad_v12_abi_second_for_class','classes',[("  DECL_ABI_DATA 62 0 20 0 1 nil\n","  DECL_ABI_DATA 62 0 20 0 1 nil\n  DECL_ABI_DATA 67 0 20 0 1 nil\n")],"a second DECL_ABI_DATA")
bad('bad_v12_abi_typeinfo_base_first','classes',[("  DECL_ABI_DATA 60 2 10 0 0 nil\n  DECL_ABI_DATA 61 2 20 0 1 nil\n","  DECL_ABI_DATA 61 2 20 0 1 nil\n  DECL_ABI_DATA 60 2 10 0 0 nil\n")],"so the typeinfo of its base G-id 10 comes first")
bad('bad_v12_abi_comdat_external','classes',[("DECL_ABI_DATA 61 2 20 0 1 nil","DECL_ABI_DATA 61 2 20 0 3 nil")],"COMDAT is set if and only if a definition has linkonce_odr or weak_odr linkage")
bad('bad_v12_abi_vague_declaration','classes',[("DECL_ABI_DATA 65 2 100 3 3 nil","DECL_ABI_DATA 65 2 100 3 0 nil")],"linkonce_odr/weak_odr is only for definitions")
bad('bad_v12_abi_vague_without_comdat','classes',[("DECL_ABI_DATA 65 2 100 3 3 nil","DECL_ABI_DATA 65 2 100 3 1 nil")],"COMDAT is set if and only if")
bad('bad_v12_abi_reissue_other_kind','classes',[("  DECL_ABI_DATA 62 0 20 0 1 nil\n","  DECL_ABI_DATA 62 0 20 0 1 nil\n  DECL_ABI_DATA 62 2 20 0 1 nil\n")],"cannot be re-issued as this DECL_ABI_DATA")
bad('bad_v12_gref_abi_data','classes',[("  CONST_INT 1 1\n  CALL 33 13 2 16\n","  GREF 60\n  DROP\n  CONST_INT 1 1\n  CALL 33 13 2 16\n")],"GREF names a G-id that was not declared")
bad('bad_v12_call_virtual_unlisted','classes',[("CALL 33 13 2 16","CALL 34 12 1 16")],"which no CLASS_VFUNCS lists")
bad('bad_v12_call_vtt_missing','classes',[("CALL 53 112 1 32","CALL 53 112 1 0")],"CALL VTT is set exactly when the callee is the base variant of a has_vbases STRUCTOR pair")
bad('bad_v12_call_vtt_extra','classes',[("CALL 51 102 1 0","CALL 51 102 1 32")],"CALL VTT is set exactly when the callee is the base variant of a has_vbases STRUCTOR pair")
bad('bad_v12_call_flag_bits','classes',[("CALL 33 13 2 16","CALL 33 13 2 64")],"CALL operand 4: unknown flag bits 0x40")
bad('bad_v12_delete_virtual_not_deleting','classes',[("  CONST_INT 1 1\n  CALL 33 13 2 16\n","  CONST_NULL 11\n  DELETE 36 34 10 8\n  CONST_INT 1 1\n  CALL 33 13 2 16\n")],"DELETE VIRTUAL needs the virtual destructor of a STRUCTOR_DELETING")
bad('bad_v12_delete_virtual_other_function','classes',[("  CONST_INT 1 1\n  CALL 33 13 2 16\n","  CONST_NULL 11\n  DELETE 70 30 10 8\n  CONST_INT 1 1\n  CALL 33 13 2 16\n")],"and its g_delete as the function")
bad('bad_v12_vbase_cast_not_vbase','classes',[("VBASE_CAST 110 100 3","VBASE_CAST 110 10 3")],"has no VIRTUAL_BASE field of G-id 10")
bad('bad_v12_vbase_cast_flag_bits','classes',[("VBASE_CAST 110 100 3","VBASE_CAST 110 100 4")],"VBASE_CAST operand 3: unknown flag bits 0x4")
bad('bad_v12_vtt_addr_kind','classes',[("VTT_ADDR 1 0","VTT_ADDR 2 0")],"VTT_ADDR idx is not a field of the right kind")
bad('bad_v12_vtt_addr_self_index','classes',[("VTT_ADDR 1 0","VTT_ADDR 0 0")],"VTT_ADDR self has no index")
bad('bad_v12_vtt_addr_no_vbases','classes',[("  FUNC_BEGIN 70 0\n  PARAM 1 11 11 36 0\n","  FUNC_BEGIN 70 0\n  PARAM 1 11 11 36 0\n  VTT_ADDR 0 nil\n")],"VTT_ADDR outside a function of a class with virtual bases")
bad('bad_v12_field_of_vbase','classes',[("  CONST_INT 1 1\n  CALL 33 13 2 16\n","  FIELD 110 2\n  DROP\n  CONST_INT 1 1\n  CALL 33 13 2 16\n")],"FIELD of a VIRTUAL_BASE field")
bad('bad_v12_c_sel_vptr','classes',[("  DECL_FUNC 70 130 0 1 35\n","  DECL_FUNC 70 130 0 1 35\n  DECL_VAR 200 10 0 1 8 nil\n  C_AGG_BEGIN 10\n  C_SEL 0\n  C_NULL 1\n  C_AGG_END\n  VAR_INIT 200\n")],"C_SEL of the vptr field")
bad('bad_v12_c_sel_vbase','classes',[("  DECL_FUNC 70 130 0 1 35\n","  DECL_FUNC 70 130 0 1 35\n  DECL_VAR 200 110 0 1 8 nil\n  C_AGG_BEGIN 110\n  C_SEL 2\n  C_NULL 1\n  C_AGG_END\n  VAR_INIT 200\n")],"selector of a VIRTUAL_BASE field")
bad('bad_v12_c_zero_dynamic','classes',[("  DECL_FUNC 70 130 0 1 35\n","  DECL_FUNC 70 130 0 1 35\n  DECL_VAR 200 10 0 1 8 nil\n  C_ZERO 10\n  VAR_INIT 200\n")],"C_ZERO of a dynamic class")
bad('bad_v12_c_agg_before_facts','classes',[("  CLASS_OVERRIDES 20 [33, 0, 42, 0, 30, 0, 43, 0]\n  DECL_ABI_DATA 60 2 10 0 0 nil\n","  DECL_VAR 200 20 0 1 8 nil\n  C_AGG_BEGIN 20\n  C_SEL 0\n  C_AGG_BEGIN 10\n  C_AGG_END\n  C_AGG_END\n  VAR_INIT 200\n  CLASS_OVERRIDES 20 [33, 0, 42, 0, 30, 0, 43, 0]\n  DECL_ABI_DATA 60 2 10 0 0 nil\n")],"must come before a constant of the class (C_AGG_BEGIN)")
# --- structor prologue
def structor(name, pairs, expect):
  bad(name,'classes',pairs,expect)
structor('bad_v12_vptr_init_missing',[("  CLEANUP_BODY\n  VPTR_INIT\n  MEMINIT_BEGIN 2 1 1","  CLEANUP_BODY\n  MEMINIT_BEGIN 2 1 1")],"VPTR_INIT comes before the first field MEMINIT")
structor('bad_v12_vptr_init_missing_body',[("  CLEANUP_BODY\n  VPTR_INIT\n  MEMINIT_BEGIN 2 1 1\n  LREF 1\n  LOAD 0\n  DEREF\n  FIELD 20 1\n  CONST_INT 1 5\n  STORE 2 0\n  MEMINIT_END\n  BLOCK_BEGIN","  CLEANUP_BODY\n  BLOCK_BEGIN")],"VPTR_INIT is missing from the prologue of a constructor or destructor of a dynamic class")
structor('bad_v12_vptr_init_missing_empty',[("  FUNC_BEGIN 53 0\n  PARAM 1 111 111 nil 1\n  VPTR_INIT\n  FUNC_END 1\n","  FUNC_BEGIN 53 0\n  PARAM 1 111 111 nil 1\n  FUNC_END 1\n")],"VPTR_INIT is missing from the prologue of a constructor or destructor of a dynamic class")
structor('bad_v12_vptr_init_twice',[("  CLEANUP_BODY\n  VPTR_INIT\n","  CLEANUP_BODY\n  VPTR_INIT\n  VPTR_INIT\n")],"VPTR_INIT appears twice")
structor('bad_v12_vptr_init_after_field_group',[("  CLEANUP_BODY\n  VPTR_INIT\n  MEMINIT_BEGIN 2 1 1\n  LREF 1\n  LOAD 0\n  DEREF\n  FIELD 20 1\n  CONST_INT 1 5\n  STORE 2 0\n  MEMINIT_END\n","  CLEANUP_BODY\n  MEMINIT_BEGIN 2 1 1\n  LREF 1\n  LOAD 0\n  DEREF\n  FIELD 20 1\n  CONST_INT 1 5\n  STORE 2 0\n  MEMINIT_END\n  VPTR_INIT\n")],"VPTR_INIT comes before the first field MEMINIT")
structor('bad_v12_vptr_init_in_function',[("  FUNC_BEGIN 70 0\n  PARAM 1 11 11 36 0\n","  FUNC_BEGIN 70 0\n  PARAM 1 11 11 36 0\n  VPTR_INIT\n")],"VPTR_INIT outside a constructor or destructor of a dynamic class")
structor('bad_v12_vptr_init_after_body',[("  BLOCK_BEGIN\n  BLOCK_END\n  FUNC_END 1\n  FUNC_BEGIN 51 0","  BLOCK_BEGIN\n  BLOCK_END\n  VPTR_INIT\n  FUNC_END 1\n  FUNC_BEGIN 51 0")],"VPTR_INIT only in the prologue of a constructor or destructor")
structor('bad_v12_vptr_init_dtor_d1_synthesized',[("  FUNC_BEGIN 53 0\n  PARAM 1 111 111 nil 1\n  VPTR_INIT\n  FUNC_END 1\n","  FUNC_BEGIN 53 0\n  PARAM 1 111 111 nil 1\n  VPTR_INIT\n  FUNC_END 1\n  FUNC_BEGIN 54 0\n  PARAM 1 111 111 nil 1\n  VPTR_INIT\n  FUNC_END 1\n")],"VPTR_INIT in the synthesized D1")
structor('bad_v12_meminit_kind_order',[("  FUNC_BEGIN 52 0\n  PARAM 1 111 111 nil 1\n  MEMINIT_BEGIN 0 2 1\n  LREF 1\n  LOAD 0\n  VBASE_CAST 110 100 3\n  CALL 51 102 1 0\n  MEMINIT_END\n","  FUNC_BEGIN 52 0\n  PARAM 1 111 111 nil 1\n  MEMINIT_BEGIN 1 0 1\n  MEMINIT_END\n  MEMINIT_BEGIN 0 2 1\n  LREF 1\n  LOAD 0\n  VBASE_CAST 110 100 3\n  CALL 51 102 1 0\n  MEMINIT_END\n")],"MEMINIT kinds appear in the order vbase, base, field")
structor('bad_v12_meminit_vbase_in_c2',[("  FUNC_BEGIN 53 0\n  PARAM 1 111 111 nil 1\n  VPTR_INIT\n","  FUNC_BEGIN 53 0\n  PARAM 1 111 111 nil 1\n  MEMINIT_BEGIN 0 2 1\n  MEMINIT_END\n  VPTR_INIT\n")],"MEMINIT_BEGIN of a virtual base is only for the complete constructor")
structor('bad_v12_meminit_vbase_without_vbases',[("  FUNC_BEGIN 41 0\n  PARAM 1 21 21 nil 1\n","  FUNC_BEGIN 41 0\n  PARAM 1 21 21 nil 1\n  MEMINIT_BEGIN 0 0 1\n  MEMINIT_END\n")],"MEMINIT_BEGIN of a virtual base is only for the complete constructor")
structor('bad_v12_meminit_idx_same_kind',[("  FUNC_BEGIN 57 0\n  PARAM 1 121 121 nil 1\n","  FUNC_BEGIN 57 0\n  PARAM 1 121 121 nil 1\n  MEMINIT_BEGIN 1 0 1\n  MEMINIT_END\n")],"MEMINIT idx must strictly increase within a kind")
structor('bad_v12_vptr_init_delegating',[("  FUNC_BEGIN 41 0\n  PARAM 1 21 21 nil 1\n  MEMINIT_BEGIN 1 0 1\n","  FUNC_BEGIN 41 0\n  PARAM 1 21 21 nil 1\n  MEMINIT_BEGIN 3 0 0\n  MEMINIT_END\n  VPTR_INIT\n  MEMINIT_BEGIN 1 0 1\n")],"VPTR_INIT in a delegating constructor")
structor('bad_v12_delegation_after_vptr_init',[("  FUNC_BEGIN 41 0\n  PARAM 1 21 21 nil 1\n  MEMINIT_BEGIN 1 0 1\n","  FUNC_BEGIN 41 0\n  PARAM 1 21 21 nil 1\n  VPTR_INIT\n  MEMINIT_BEGIN 3 0 0\n  MEMINIT_END\n  MEMINIT_BEGIN 1 0 1\n")],"a delegating MEMINIT must be the only initializer group")

# --- member pointers
bad('bad_v12_pm_field_not_data_member','memptr',[("PM_FIELD 7","PM_FIELD 8")],"PM_FIELD needs a TY_DATA_MEMBER type")
bad('bad_v12_pm_cast_kind_mismatch','memptr',[("PM_CAST 0 7 9 8","PM_CAST 0 7 10 8")],"PM_CAST converts between two member pointer types of one kind")
bad('bad_v12_pm_cast_kind_range','memptr',[("PM_CAST 0 7 9 8","PM_CAST 2 7 9 8")],"PM_CAST operand 1: value 2 out of range")
bad('bad_v12_pm_call_nargs','memptr',[("PM_CALL 6 1 0","PM_CALL 6 0 0")],"PM_CALL nargs counts the parameters of its function type")
bad('bad_v12_pm_call_not_function','memptr',[("PM_CALL 6 1 0","PM_CALL 7 1 0")],"PM_CALL needs a function type")
bad('bad_v12_const_data_member_type','memptr',[("CONST_DATA_MEMBER 7 4","CONST_DATA_MEMBER 5 4")],"CONST_DATA_MEMBER needs a TY_DATA_MEMBER type")
bad('bad_v12_const_method_type','memptr',[("CONST_METHOD 8 20 0 0","CONST_METHOD 7 20 0 0")],"CONST_METHOD needs a TY_METHOD type")
bad('bad_v12_const_method_virtual_unlisted','memptr',[("CONST_METHOD 8 20 0 0","CONST_METHOD 8 20 0 1")],"CONST_METHOD VIRTUAL needs a function that a CLASS_VFUNCS lists")
bad('bad_v12_const_method_not_function','memptr',[("CONST_METHOD 8 20 0 0","CONST_METHOD 8 5 0 0")],"which is not a declared function")
bad('bad_v12_const_method_null_with_adj','memptr',[("CONST_METHOD 8 20 8 0\n  DROP\n  CONST_INT 1 0","CONST_METHOD 8 nil 8 0\n  DROP\n  CONST_INT 1 0")],"the null method pointer has no adjustment and is not virtual")
bad('bad_v12_c_method_adjustment_in_body','memptr',[("  CONST_INT 1 0\n  RETURN_STMT true\n","  LOCAL 2 8 nil 8 0\n  LREF 2\n  C_METHOD 8 20 8 0\n  INIT_CONST\n  CONST_INT 1 0\n  RETURN_STMT true\n")],"C_METHOD with a non-zero adjustment in a function body")
bad('bad_v12_ty_method_wrong_class','memptr',[("TY_METHOD 8 6 3","TY_METHOD 8 6 4")],"TY_METHOD needs a function type whose first parameter points to the class")
bad('bad_v12_ty_method_class_not_record','memptr',[("TY_METHOD 8 6 3","TY_METHOD 8 6 1")],"names a class G-id that is not a record")
bad('bad_v12_ty_data_member_function_type','memptr',[("TY_DATA_MEMBER 7 1 3","TY_DATA_MEMBER 7 6 3")],"TY_DATA_MEMBER of a function or void type")
bad('bad_v12_c_data_member_type','memptr',[("  C_DATA_MEMBER 7 4\n","  C_DATA_MEMBER 8 4\n")],"C_DATA_MEMBER needs a TY_DATA_MEMBER type")

# --- the minor
emit('bad_v12_record_in_minor1','format',"TY_VPTR: record needs ECIL minor >= 2","""ecil 1.1 features=0x00000000
frame HEADER
  STR 1 "x86_64-unknown-linux-gnu"
  HDR_TARGET 1 nil nil 64 64 6
  HDR_LANG 1 201703 0
frame DECLS
  TY_VPTR 1
frame END
  END 2 0
""")
emit('bad_v12_init_zero_base_minor1','format',"INIT_ZERO_BASE: record needs ECIL minor >= 2","ecil 1.1 features=0x00000000\nframe HEADER\n  STR 1 \"x86_64-unknown-linux-gnu\"\n  HDR_TARGET 1 nil nil 64 64 6\n  HDR_LANG 1 201703 0\nframe REGION\n  TY_VOID 2\n  STR 2 \"f\"\n  TY_FUNC 3 2 0 []\n  DECL_FUNC 20 3 0 1 2\n  FUNC_BEGIN 20 0\n  INIT_ZERO_BASE\n  FUNC_END 1\nframe END\n  END 2 0\n")
emit('bad_v12_exceptions_minor1','format',"HDR_LANG: C++ with exceptions needs ECIL minor >= 2","""ecil 1.1 features=0x00000000
frame HEADER
  STR 1 "x86_64-unknown-linux-gnu"
  HDR_TARGET 1 nil nil 64 64 6
  HDR_LANG 1 201703 2
frame END
  END 2 0
""")

# --- function-try-block constructors
FT_OPEN=("  FUNC_BEGIN 41 0\n  PARAM 1 21 21 nil 1\n  MEMINIT_BEGIN 1 0 1","  FUNC_BEGIN 41 0\n  PARAM 1 21 21 nil 1\n  TRY_BEGIN 1\n  MEMINIT_BEGIN 1 0 1")
FT_CLOSE=("  BLOCK_BEGIN\n  BLOCK_END\n  FUNC_END 1\n  FUNC_BEGIN 51","  BLOCK_BEGIN\n  BLOCK_END\n  CATCH_BEGIN nil\n  BLOCK_BEGIN\n  RETHROW\n  BLOCK_END\n  TRY_END\n  FUNC_END 1\n  FUNC_BEGIN 51")
open(f'{OUT}/good_v12_function_try.txt','w').write("; ECIL 1.2: a function-try-block constructor: the member initializers and VPTR_INIT inside the try body, a catch-all that rethrows.\n"+mut('classes',[FT_OPEN,FT_CLOSE]))
bad('bad_v12_function_try_late','classes',[("  FUNC_BEGIN 41 0\n  PARAM 1 21 21 nil 1\n  MEMINIT_BEGIN 1 0 1\n  LREF 1\n  LOAD 0\n  BASE_CAST 20 10 0 1\n  CALL 35 12 1 0\n  MEMINIT_END\n","  FUNC_BEGIN 41 0\n  PARAM 1 21 21 nil 1\n  MEMINIT_BEGIN 1 0 1\n  LREF 1\n  LOAD 0\n  BASE_CAST 20 10 0 1\n  CALL 35 12 1 0\n  MEMINIT_END\n  TRY_BEGIN 1\n"),("  BLOCK_BEGIN\n  BLOCK_END\n  FUNC_END 1\n  FUNC_BEGIN 51","  BLOCK_BEGIN\n  BLOCK_END\n  CATCH_BEGIN nil\n  TRY_END\n  FUNC_END 1\n  FUNC_BEGIN 51")],"TRY_BEGIN FUNCTION_TRY comes right after the function header")
bad('bad_v12_function_try_without_vptr_init','classes',[FT_OPEN,FT_CLOSE,("  CLEANUP_BODY\n  VPTR_INIT\n  MEMINIT_BEGIN 2 1 1\n  LREF 1\n  LOAD 0\n  DEREF\n  FIELD 20 1\n  CONST_INT 1 5\n  STORE 2 0\n  MEMINIT_END\n  BLOCK_BEGIN\n  BLOCK_END\n  CATCH_BEGIN","  CLEANUP_BODY\n  CATCH_BEGIN")],"VPTR_INIT is missing from the prologue of a constructor or destructor of a dynamic class")
bad('bad_v12_function_try_twice','classes',[FT_OPEN,("  FUNC_BEGIN 41 0\n  PARAM 1 21 21 nil 1\n  TRY_BEGIN 1\n","  FUNC_BEGIN 41 0\n  PARAM 1 21 21 nil 1\n  TRY_BEGIN 1\n  TRY_BEGIN 1\n")],"TRY_BEGIN FUNCTION_TRY comes right after the function header")
bad('bad_v12_function_try_in_synthesized_d1','classes',[("  FUNC_BEGIN 53 0\n  PARAM 1 111 111 nil 1\n  VPTR_INIT\n  FUNC_END 1\n","  FUNC_BEGIN 53 0\n  PARAM 1 111 111 nil 1\n  VPTR_INIT\n  FUNC_END 1\n  FUNC_BEGIN 54 0\n  PARAM 1 111 111 nil 1\n  TRY_BEGIN 1\n")],"only for the body of a constructor or destructor")
bad('bad_v12_catch_handler_meminit','classes',[FT_OPEN,FT_CLOSE,("  CATCH_BEGIN nil\n  BLOCK_BEGIN\n  RETHROW\n  BLOCK_END\n  TRY_END\n  FUNC_END 1\n  FUNC_BEGIN 51","  CATCH_BEGIN nil\n  MEMINIT_BEGIN 2 1 1\n  MEMINIT_END\n  BLOCK_BEGIN\n  RETHROW\n  BLOCK_END\n  TRY_END\n  FUNC_END 1\n  FUNC_BEGIN 51")],"MEMINIT_BEGIN only starts a constructor body")

# --- first need (the M4a re-review): the vtable facts and everything else
# that is not a layout fact of a class with a vptr or a virtual base are sent
# at their first need.  CLS_HDR: class V (own vptr, a virtual f) with its
# layout facts, its functions and a constructor; the vtable facts are the
# part that the tests add or leave out.
CLS_HDR = """ecil 1.2 features=0x00000000
frame HEADER
  STR 1 "x86_64-unknown-linux-gnu"
  HDR_TARGET 1 nil nil 64 64 6
  HDR_LANG 1 201703 6
frame REGION
  TY_INT 1 32 true 4 4
  TY_VOID 2
  TY_VPTR 3
  STR 2 "1V"
  STR 3 "_ZN1V1fEv"
  STR 4 "_ZN1VC1Ev"
  STR 5 "_ZN1VC2Ev"
  STR 6 "v"
  STR 7 "_Z1gv"
  STR 8 "_ZN1VC1ERKS_"
  TY_RECORD_DECL 10 0 nil
  TY_RECORD_BODY 10 16 12 8 0 2
  REC_FIELD 3 0 nil nil nil 1
  REC_FIELD 1 8 nil nil 6 0
  CLASS_MANGLE 10 2
  CLASS_INFO 10 12 8 nil 1
  CLASS_BASES 10 []
  TY_PTR 11 10
  TY_FUNC 12 2 0 [11]
  TY_FUNC 13 2 0 []
  TY_FUNC 14 2 0 [11, 11]
  DECL_FUNC 30 12 0 1 3
  DECL_FUNC 34 12 0 1 4
  DECL_FUNC 35 12 0 1 5
  STRUCTOR 34 35 0
  DECL_FUNC 36 14 0 1 8
  DECL_FUNC 70 13 0 1 7
"""
VT = "  CLASS_VFUNCS 10 [30, 0, 0, 1]\n  CLASS_OVERRIDES 10 []\n"
def cls(name, kind, expect, before, after, comment=None):
  emit(name, kind, expect, CLS_HDR + before + after + "frame END\n  END 2 0\n", comment)
FN_VPTR = "  FUNC_BEGIN 35 0\n  PARAM 1 11 11 nil 1\n  VPTR_INIT\n  FUNC_END 1\n"
FN_CALL = "  FUNC_BEGIN 70 0\n  LOCAL 1 11 nil 4 0\n  LREF 1\n  LOAD 0\n  CALL 30 12 1 16\n  DROP\n  FUNC_END 1\n"
FN_CATCH = "  FUNC_BEGIN 70 0\n  TRY_BEGIN 0\n  CATCH_BEGIN nil\n  LOCAL 1 10 nil 4 0\n  CATCH_PARAM 1 5 36\n  TRY_END\n  FUNC_END 1\n"
for nm, fn, what in (('vptr_init', FN_VPTR, 'VPTR_INIT'), ('call_virtual', FN_CALL, 'a virtual call'), ('catch_param', FN_CATCH, 'CATCH_PARAM')):
  open(f'{OUT}/good_v12_first_need_{nm}.txt','w').write("; ECIL 1.2: the vtable facts of V are sent at their first need, here just before "+what+".\n"+CLS_HDR+VT+fn+"frame END\n  END 2 0\n")
  cls('bad_v12_first_need_'+nm, 'format', "which no CLASS_VFUNCS lists" if nm == 'call_virtual' else "must come before "+what, '', fn)
# the vtable facts are half there
cls('bad_v12_first_need_call_virtual_no_overrides', 'format', "must come before a virtual call", "  CLASS_VFUNCS 10 [30, 0, 0, 1]\n", FN_CALL)
# DYNAMIC_CAST goes through the typeinfo of a class, whose DECL_ABI_DATA needs the same facts
cls('bad_v12_first_need_dynamic_cast', 'format', "must come before DECL_ABI_DATA of the class", '', "  STR 20 \"_ZTI1V\"\n  DECL_ABI_DATA 60 2 10 0 0 nil\n  FUNC_BEGIN 70 0\n  LOCAL 1 11 nil 4 0\n  LREF 1\n  LOAD 0\n  DYNAMIC_CAST 60 60 0 0\n  DROP\n  FUNC_END 1\n")
# typeinfo of a class that is incomplete, and has no mangling
emit('bad_v12_abi_class_typeinfo_without_mangle', 'format', "needs its CLASS_MANGLE first", """ecil 1.2 features=0x00000000
frame HEADER
  STR 1 "x86_64-unknown-linux-gnu"
  HDR_TARGET 1 nil nil 64 64 6
  HDR_LANG 1 201703 2
frame REGION
  TY_RECORD_DECL 10 0 nil
  DECL_ABI_DATA 60 2 10 1 0 nil
frame END
  END 2 0
""")
# bases first, within each group (12.2): the two shapes of the M4a re-review
BASES_OK = """; ECIL 1.2: (1) struct V { virtual void f(); int v; }; struct D : virtual V {}; D is laid out right away and V's vtable facts never come; (2) struct Q {int q;}; struct P : Q {int p;}; struct E : P { virtual void f(); }; E is laid out right away and P has no layout facts at all.
ecil 1.2 features=0x00000000
frame HEADER
  STR 1 "x86_64-unknown-linux-gnu"
  HDR_TARGET 1 nil nil 64 64 6
  HDR_LANG 1 201703 0
frame REGION
  TY_INT 1 32 true 4 4
  TY_VPTR 3
  STR 2 "1V"
  STR 3 "1D"
  STR 4 "1E"
  STR 5 "v"
  TY_RECORD_DECL 10 0 nil
  TY_RECORD_BODY 10 16 12 8 0 2
  REC_FIELD 3 0 nil nil nil 1
  REC_FIELD 1 8 nil nil 5 0
  CLASS_MANGLE 10 2
  CLASS_INFO 10 12 8 nil 1
  CLASS_BASES 10 []
  TY_RECORD_DECL 20 0 nil
  TY_RECORD_BODY 20 32 32 8 0 2
  REC_FIELD 3 0 nil nil nil 1
  REC_FIELD 10 16 nil nil nil 64
  CLASS_MANGLE 20 3
  CLASS_INFO 20 8 8 nil 0
  CLASS_BASES 20 [1, 1]
  TY_RECORD_DECL 30 0 nil
  TY_RECORD_BODY 30 4 4 4 0 1
  REC_FIELD 1 0 nil nil nil 0
  TY_RECORD_DECL 31 0 nil
  TY_RECORD_BODY 31 8 8 4 0 2
  REC_FIELD 30 0 nil nil nil 16
  REC_FIELD 1 4 nil nil 5 0
  TY_RECORD_DECL 32 0 nil
  TY_RECORD_BODY 32 16 16 8 0 2
  REC_FIELD 3 0 nil nil nil 1
  REC_FIELD 31 8 nil nil nil 16
  CLASS_MANGLE 32 4
  CLASS_INFO 32 16 8 nil 1
  CLASS_BASES 32 [1, 1]
frame END
  END 2 0
"""
open(f'{OUT}/good_v12_bases_first_layout_only.txt','w').write(BASES_OK)
# CLASS_INFO: a dynamic class has its own TY_VPTR or a primary_idx, never both and never neither
def sa(name, expect, fields, info, comment=None):
  # X and Z: plain classes; Y (G-id 40) is dynamic through the virtual base Z
  txt = """ecil 1.2 features=0x00000000
frame HEADER
  STR 1 "x86_64-unknown-linux-gnu"
  HDR_TARGET 1 nil nil 64 64 6
  HDR_LANG 1 201703 0
frame REGION
  TY_INT 1 32 true 4 4
  TY_VPTR 3
  STR 2 "1Y"
  TY_RECORD_DECL 30 0 nil
  TY_RECORD_BODY 30 4 4 4 0 1
  REC_FIELD 1 0 nil nil nil 0
  TY_RECORD_DECL 31 0 nil
  TY_RECORD_BODY 31 4 4 4 0 1
  REC_FIELD 1 0 nil nil nil 0
""" + fields + "  CLASS_MANGLE 40 2\n" + info + "frame END\n  END 2 0\n"
  emit(name, 'format', expect, txt, comment)
Y_FIELDS = "  TY_RECORD_DECL 40 0 nil\n  TY_RECORD_BODY 40 8 8 4 0 2\n  REC_FIELD 30 0 nil nil nil 16\n  REC_FIELD 31 4 nil nil nil 64\n"
sa('bad_v12_class_info_primary_not_dynamic_base', "names a base that is not a dynamic class", Y_FIELDS, "  CLASS_INFO 40 4 4 0 0\n")
sa('bad_v12_class_info_dynamic_neither', "a dynamic class has either its own TY_VPTR field or a primary_idx, never neither", Y_FIELDS, "  CLASS_INFO 40 4 4 nil 0\n")
# a primary virtual base and an own vptr
emit('bad_v12_class_info_dynamic_both', 'format', "a dynamic class has either its own TY_VPTR field or a primary_idx, never both", """ecil 1.2 features=0x00000000
frame HEADER
  STR 1 "x86_64-unknown-linux-gnu"
  HDR_TARGET 1 nil nil 64 64 6
  HDR_LANG 1 201703 0
frame REGION
  TY_INT 1 32 true 4 4
  TY_VPTR 3
  STR 2 "1V"
  STR 3 "1Y"
  TY_RECORD_DECL 10 0 nil
  TY_RECORD_BODY 10 8 8 8 0 1
  REC_FIELD 3 0 nil nil nil 1
  CLASS_MANGLE 10 2
  CLASS_INFO 10 8 8 nil 1
  CLASS_BASES 10 []
  TY_RECORD_DECL 20 0 nil
  TY_RECORD_BODY 20 16 16 8 0 2
  REC_FIELD 3 0 nil nil nil 1
  REC_FIELD 10 0 nil nil nil 16
  CLASS_MANGLE 20 3
  CLASS_INFO 20 16 8 1 1
  CLASS_BASES 20 [1, 1]
frame END
  END 2 0
""")
# DECL_ABI_DATA TU_LOCAL (user decision 2026-10-12): typeinfo only, internal linkage
open(f'{OUT}/good_v12_abi_tu_local.txt','w').write("; ECIL 1.2: DECL_ABI_DATA TU_LOCAL 0x04 on the typeinfo of a class, with internal linkage.\n"+mut('classes',[("DECL_ABI_DATA 60 2 10 0 0 nil","DECL_ABI_DATA 60 2 10 1 4 nil")]))
bad('bad_v12_abi_tu_local_external','classes',[("DECL_ABI_DATA 60 2 10 0 0 nil","DECL_ABI_DATA 60 2 10 0 4 nil")],"DECL_ABI_DATA flag TU_LOCAL implies internal linkage")
bad('bad_v12_abi_tu_local_vtable','classes',[("DECL_ABI_DATA 62 0 20 0 1 nil","DECL_ABI_DATA 62 0 20 1 5 nil")],"DECL_ABI_DATA flag TU_LOCAL is for a typeinfo only")
bad('bad_v12_abi_flag_bits','classes',[("DECL_ABI_DATA 60 2 10 0 0 nil","DECL_ABI_DATA 60 2 10 0 8 nil")],"DECL_ABI_DATA operand 5: unknown flag bits 0x8")
# a re-issue never clears NOTHROW
eh('bad_v12_decl_func_nothrow_cleared', "", "re-issue of DECL_FUNC G-id 21 clears NOTHROW", decl="  DECL_FUNC 21 3 0 1 4\n")


# --- dynamic is the right-away predicate (12.2, the r3 re-review): a class
# that is dynamic only through its primary base has its layout facts right
# after its body, and its derived classes' CLASS_BASES need them.
DYN_BASE = """ecil 1.2 features=0x00000000
frame HEADER
  STR 1 "x86_64-unknown-linux-gnu"
  HDR_TARGET 1 nil nil 64 64 6
  HDR_LANG 1 201703 0
frame REGION
  TY_INT 1 32 true 4 4
  TY_VPTR 3
  STR 2 "1A"
  STR 3 "1B"
  STR 4 "1V"
  STR 5 "1D"
  STR 6 "1E"
  STR 7 "v"
  TY_RECORD_DECL 10 0 nil
  TY_RECORD_BODY 10 8 8 8 0 1
  REC_FIELD 3 0 nil nil nil 1
  CLASS_MANGLE 10 2
  CLASS_INFO 10 8 8 nil 1
  CLASS_BASES 10 []
  TY_RECORD_DECL 20 0 nil
  TY_RECORD_BODY 20 8 8 8 0 1
  REC_FIELD 10 0 nil nil nil 16
  CLASS_MANGLE 20 3
  CLASS_INFO 20 8 8 0 1
  CLASS_BASES 20 [0, 1]
  TY_RECORD_DECL 30 0 nil
  TY_RECORD_BODY 30 4 4 4 0 1
  REC_FIELD 1 0 nil nil 7 0
  TY_RECORD_DECL 40 0 nil
  TY_RECORD_BODY 40 16 12 8 0 2
  REC_FIELD 20 0 nil nil nil 16
  REC_FIELD 30 8 nil nil nil 64
  CLASS_MANGLE 40 5
  CLASS_INFO 40 8 8 0 1
  CLASS_BASES 40 [0, 1, 1, 1]
  TY_RECORD_DECL 50 0 nil
  TY_RECORD_BODY 50 8 8 8 0 1
  REC_FIELD 20 0 nil nil nil 64
  CLASS_MANGLE 50 6
  CLASS_INFO 50 8 8 0 1
  CLASS_BASES 50 [0, 1]
frame END
  END 2 0
"""
open(f'{OUT}/good_v12_dynamic_through_base.txt','w').write("; ECIL 1.2: dynamic is the right-away predicate. struct A { virtual void f(); }; struct B : A {}; struct V { int v; }; struct D : B, virtual V {} (B is dynamic only through A, and D's primary base); struct E : virtual B {} (B a nearly empty primary virtual base). The vtable facts never come.\n"+DYN_BASE)
def dyn(name, pairs, expect):
  t = DYN_BASE
  for a, b in pairs:
    assert a in t, a
    t = t.replace(a, b, 1)
  emit(name, 'format', expect, t)
B_FACTS = "  CLASS_MANGLE 20 3\n  CLASS_INFO 20 8 8 0 1\n  CLASS_BASES 20 [0, 1]\n"
# B's facts come late: the class is dynamic through A, so they follow its body directly
dyn('bad_v12_dynamic_through_base_facts_late', [(B_FACTS, "")], "follow its TY_RECORD_BODY directly")
dyn('bad_v12_dynamic_through_base_facts_after_other_record', [(B_FACTS, ""), ("  TY_RECORD_DECL 30 0 nil\n  TY_RECORD_BODY 30 4 4 4 0 1\n  REC_FIELD 1 0 nil nil 7 0\n", "  TY_RECORD_DECL 30 0 nil\n  TY_RECORD_BODY 30 4 4 4 0 1\n  REC_FIELD 1 0 nil nil 7 0\n" + B_FACTS)], "follow its TY_RECORD_BODY directly")
dyn('bad_v12_dynamic_through_base_without_info', [("  CLASS_INFO 20 8 8 0 1\n", "")], "follow its TY_RECORD_BODY directly")
dyn('bad_v12_dynamic_through_base_primary', [("  CLASS_INFO 20 8 8 0 1\n", "  CLASS_INFO 20 8 8 nil 1\n")], "a dynamic class has either its own TY_VPTR field or a primary_idx, never neither")

print(len(made),'fixtures (with function-try)')
