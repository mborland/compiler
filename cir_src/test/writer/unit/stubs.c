#include "fe_common.h"
USING_NAMESPACE_EDG
char *ecil_output_file_name;
bool ecil_keep_regions;
void pos_st_error(an_error_code c, a_source_position *p, a_const_char *s) { (void)p; (void)c; fprintf(stderr, "ERR: %s\n", s); }
void pos_st_warning(an_error_code c, a_source_position *p, a_const_char *s) { (void)p; (void)c; fprintf(stderr, "WARN: %s\n", s); }
void str_error(an_error_code c, a_const_char *s) { (void)c; fprintf(stderr, "ERR: %s\n", s); }
void str_warning(an_error_code c, a_const_char *s) { (void)c; fprintf(stderr, "WARN: %s\n", s); }
#define DEF(v) decltype(v) v
DEF(null_source_position); DEF(highest_used_region_number); DEF(mem_region_table); DEF(targ_supports_x86_64); DEF(targ_supports_arm64); DEF(targ_supports_arm32); DEF(targ_supports_riscv64); DEF(targ_supports_riscv32); DEF(C_dialect); DEF(diagnostic_counters); DEF(exceptions_enabled);
DEF(primary_source_file_name); DEF(rtti_enabled); DEF(std_version);
DEF(targ_alignof_int); DEF(targ_char_bit); DEF(targ_has_signed_chars);
DEF(targ_little_endian); DEF(targ_sizeof_int); DEF(targ_sizeof_long);
DEF(targ_sizeof_pointer); DEF(targ_sizeof_double); DEF(targ_alignof_double);
DEF(curr_translation_unit); DEF(cleared_locator);
void assertion_failed(const char *a, int b, const char *c, const char *d, const char *e) { fprintf(stderr, "ASSERT %s:%d %s %s %s\n", a, b, c?c:"", d?d:"", e?e:""); abort(); }

/* Symbols the type/constant/code modules reference; the harness never
   reaches them (the integration harness runs the real front end). */
DEF(il_header); DEF(curr_il_region_number); DEF(innermost_function_scope);
decltype(int_kind_is_signed) int_kind_is_signed;
a_type_qualifier_set f_get_type_qualifiers(a_type_ptr tp, a_boolean top) {
  (void)tp; (void)top; abort(); return 0; }
a_host_fp_value fetch_host_fp_value(a_float_kind k,
                                    an_internal_float_value *v) {
  (void)k; (void)v; abort(); }
void get_variable_initializer(a_variable_ptr v, a_scope_ptr s,
                              an_init_kind *k, an_initializer_ptr *i) {
  (void)v; (void)s; (void)k; (void)i; abort(); }
a_host_large_integer value_of_integer_constant(a_constant *c, a_boolean *o) {
  (void)c; (void)o; abort(); }
a_host_large_unsigned unsigned_value_of_integer_constant(a_constant *c,
                                                         a_boolean *o) {
  (void)c; (void)o; abort(); }

/* An empty file scope for ecil_emit_globals. */
static a_scope empty_primary_scope;
static void init_empty_scope(void) __attribute__((constructor));
static void init_empty_scope(void) {
  il_header.primary_scope = &empty_primary_scope;
  targ_supports_x86_64 = TRUE;
}

/* The predefined macros: the test target defines __linux__ (a fake
   translation unit with a file scope stands for "start-up is over"). */
static a_translation_unit fake_translation_unit;
static void init_fake_translation_unit(void) __attribute__((constructor));
static void init_fake_translation_unit(void) {
  fake_translation_unit.primary_scope = &empty_primary_scope;
  curr_translation_unit = &fake_translation_unit;
}
a_symbol_ptr find_macro_symbol_by_name(a_const_char *identifier,
                                       sizeof_t length,
                                       a_symbol_locator *locator) {
  (void)locator;
  return (length == 9 && memcmp(identifier, "__linux__", 9) == 0) ?
         (a_symbol_ptr)&fake_translation_unit : NULL; }
