#include "fe_common.h"
#include "overload.h"
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

/* The C++ queries of the type and class writers (not used by the C cases). */
a_type_ptr skip_array_types(a_type_ptr t) { (void)t; abort(); }
a_targ_size_t f_size_of_type(a_type_ptr t) { (void)t; abort(); }
a_boolean gcc_mode;
a_boolean use_nonstandard_for_init_scope;
a_boolean cfront_2_1_mode;
a_boolean is_reference_type(a_type_ptr t) { (void)t; abort(); }
a_boolean is_union_type(a_type_ptr t) { (void)t; abort(); }
a_type_ptr f_implicit_this_param_type_of(a_type_ptr t) { (void)t; abort(); }
a_targ_size_t data_size_of_type(a_type_ptr t) { (void)t; abort(); }
a_boolean is_copy_constructor(a_routine_ptr r, a_type_ptr t,
                              a_type_qualifier_set *q, a_boolean a,
                              a_boolean b) {
  (void)r; (void)t; (void)q; (void)a; (void)b; abort(); }
a_boolean is_void_type(a_type_ptr t) { (void)t; abort(); }
/* new and delete (not used by the C cases). */
a_targ_alignment f_alignment_of_type(a_type_ptr t) { (void)t; abort(); }
a_symbol_ptr find_default_operator_new_sym(a_symbol_ptr s, a_boolean *a) {
  (void)s; (void)a; abort(); }
a_boolean is_default_operator_delete(a_routine_ptr r, a_boolean *a,
                                     a_boolean *b, a_boolean *c) {
  (void)r; (void)a; (void)b; (void)c; abort(); }
a_boolean is_void_star_type(a_type_ptr t) { (void)t; abort(); }
a_boolean new_or_delete_type_requires_array_handling(a_type_ptr t,
                                                     a_boolean c) {
  (void)t; (void)c; abort(); }
a_symbol_ptr opname_function_symbol(an_opname_kind k) { (void)k; abort(); }
a_symbol_ptr set_up_overload_set_traversal(a_symbol_ptr s,
                    a_candidate_function **c, a_symbol_ptr *p,
                    an_overload_set_traversal_block *b) {
  (void)s; (void)c; (void)p; (void)b; abort(); }
a_symbol_ptr next_symbol_in_overload_set(an_overload_set_traversal_block *b) {
  (void)b; abort(); }
/* Structured bindings (not used by the C cases). */
an_expr_node_ptr copy_expr_tree(an_expr_node_ptr n, int o) {
  (void)n; (void)o; abort(); }
an_expr_node_ptr rvalue_expr_for_lvalue(an_expr_node_ptr n) {
  (void)n; abort(); }
an_expr_node_ptr add_address_of_to_node(an_expr_node_ptr n) {
  (void)n; abort(); }
a_type_ptr type_pointed_to(a_type_ptr t) { (void)t; abort(); }
/* The built-in <=> (not used by the C cases). */
a_constant_ptr strong_ordering_equal, strong_ordering_less,
               strong_ordering_greater, weak_ordering_equivalent,
               weak_ordering_less, weak_ordering_greater,
               partial_ordering_equivalent, partial_ordering_less,
               partial_ordering_greater, partial_ordering_unordered;
a_type_ptr strong_ordering_type(void) { abort(); }
a_type_ptr weak_ordering_type(void) { abort(); }
a_type_ptr partial_ordering_type(void) { abort(); }
void initialize_ordering_constants(void) { abort(); }
a_boolean f_identical_types(a_type_ptr a, a_type_ptr b, unsigned int f) {
  (void)a; (void)b; (void)f; abort(); }
/* Class facts and the vtable decisions (not used by the C cases). */
void set_class_assoc_operator_delete_routine(a_type_ptr t) { (void)t; abort(); }
a_routine_ptr enclosing_routine_for_local_type_or_null(a_type_ptr t) {
  (void)t; abort(); }
a_boolean routine_might_exist_in_multiple_copies(a_routine_ptr r) {
  (void)r; abort(); }
a_routine_ptr final_overrider(a_routine_ptr f, an_expr_node_ptr n, a_type_ptr t) {
  (void)f; (void)n; (void)t; abort(); }
a_boolean is_empty_class_type(a_type_ptr t) { (void)t; abort(); }
/* Member pointers, typeinfo (not used by the C cases). */
a_boolean is_enum_type(a_type_ptr t) { (void)t; abort(); }
a_type_ptr pm_class_type(a_type_ptr t) { (void)t; abort(); }
a_type_ptr pm_member_type(a_type_ptr t) { (void)t; abort(); }
a_boolean is_nothrow_type(a_type_ptr t) { (void)t; abort(); }
a_boolean is_nullptr_type(a_type_ptr t) { (void)t; abort(); }
a_type_ptr function_type_without_noexcept_exception_spec(a_type_ptr t) {
  (void)t; abort(); }
a_scope_depth depth_scope_stack;
/* Exceptions (not used by the C cases). */
a_boolean is_non_throwing_routine(a_routine_ptr r) { (void)r; abort(); }
/* Placement operator delete (not used by the C cases). */
a_boolean seq_is_in_system_header(a_seq_number s) { (void)s; abort(); }
/* Pointer-to-member builtins (not used by the C cases). */
a_targ_size_t common_initial_sequence_limit(a_type_ptr a, a_type_ptr b) { (void)a; (void)b; abort(); }
/* The signature classes of a library function (pure and const keys). */
a_boolean is_pointer_type(a_type_ptr t) { (void)t; abort(); }
a_boolean is_real_floating_type(a_type_ptr t) { (void)t; abort(); }
a_boolean is_integral_or_enum_type(a_type_ptr t) { (void)t; abort(); }
/* GNU attributes (the cold and hot keys). */
an_attribute_ptr find_attribute(an_attribute_kind kind, an_attribute_ptr list) {
  (void)kind; (void)list; return NULL; }
