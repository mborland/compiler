/*
Part of the EDG Compiler Project, under the Apache License v2.0 with LLVM
Exceptions.
See https://edgcpp.org/LICENSE.txt for license information.
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
*/

/*

mem_tables.h -- Definitions of the memory management data structure.

These are definitions needed both in the front end to create the data
structure and in the back end to understand it.

*/

/* Avoid including these declarations more than once: */
#ifndef MEM_TABLES_H
#define MEM_TABLES_H 1

#ifndef HOST_ENVIR_H
#include "host_envir.h"
#endif /* ifndef HOST_ENVIR_H */

/* Conditionally open the "edg" namespace. */
BEGIN_EDG_NAMESPACE

/* Definition of numbers for memory regions. */
typedef int a_memory_region_number;
#define MAX_MEMORY_REGION_NUMBER ((a_memory_region_number)INT_MAX)

#define NO_MEMORY_REGION_NUMBER ((a_memory_region_number)-1)
#define NULL_region_number  ((a_memory_region_number)0)
#define FRONT_END_REGION_NUMBER ((a_memory_region_number)0)
/*
NO_MEMORY_REGION_NUMBER is used to indicate the absence of a memory region.

NULL_region_number is also used for the region of information used in
the front end and not written out or otherwise passed to the back end

FILE_SCOPE_REGION_NUMBER is the memory region number for the
file scope.  Note that in the front end one should use the global
variable file_scope_region_number if a secondary translation unit
might be involved.
*/
#define FILE_SCOPE_REGION_NUMBER ((a_memory_region_number)1)

EXTERN_THREAD a_memory_region_number
		file_scope_region_number;
			/* The memory region number for the file scope.
			   Equal to FILE_SCOPE_REGION_NUMBER except when
			   processing a secondary translation unit (e.g.,
			   for export template). */

/*
Definition for a function number.  Functions are stored in function
memory regions, but all function definitions within a top-level (i.e.,
namespace scope or non-local class scope) function are stored in the
same memory region.

When IL is written to a file, it must be possible to get from a function to
its associated scope.  This is done using the memory region and the
function number.  The memory region (from the routine entry) is used to
read the memory region.  The process of reading the IL populates the
function definition table for the functions in the memory region. The
function number is used as an index into an array that contains a pointer
to the top-level function scope.  The array also contains the memory region
number so that the memory region can be determined based only on a function
definition number (if a routine pointer is not available).
*/
typedef int a_function_def_number;
#define MAX_FUNCTION_DEF_NUMBER ((a_function_def_number)INT_MAX)
#define NULL_function_def_number  ((a_function_def_number)0)
#define NO_FUNCTION_DEF_NUMBER ((a_function_def_number)-1)
/*
NO_FUNCTION_DEF_NUMBER is used to indicate the absence of a function
definition number.
*/

typedef struct a_function_def_descr *a_function_def_descr_ptr;
typedef struct a_function_def_descr {
  struct a_scope
		*scope;
			/* Pointer to the top-level scope of the function.
			   This will be in a function scope memory region. */
  a_memory_region_number
		memory_region;
			/* The memory region number containing the function,
			   which includes the region's top-level function and
			   any other (lambda-related or lowering-added
			   "helper") functions inside the top-level function.
			   Note that all functions have a top-level scope, but
			   not all functions are top-level functions. */
} a_function_def_descr;

/*
Return the memory region for a given function definition number.
*/
#define mem_region_for_function_def(n)					\
  (il_header.function_def_table[n].memory_region)

/*
Return the scope for a given function definition number.  If the memory
region for the function has been freed, return NULL.
*/
#define scope_for_function_def(n)					\
  (mem_region_table[mem_region_for_function_def(n)] != NULL ?		\
            il_header.function_def_table[n].scope : (a_scope_ptr)NULL)

/*
Header for a block of memory.  One or more of these make up a memory
region.
*/
typedef struct a_mem_block_header *a_mem_block_header_ptr;
typedef struct a_mem_block_header {
  a_mem_block_header_ptr
		next;
			/* Pointer to the next block in the same memory region,
			   or NULL if this is the last block. */
  char		*start_of_block;
			/* Pointer to the first byte of the block (after the
			   block header). */
  char		*next_avail_in_block;
			/* Pointer to the first available byte in this
			   block. */
  char		*after_end_of_block;
			/* Pointer to just after the end of this block. */
  sizeof_t	malloc_size;
			/* If this is the start of a block allocated by malloc,
			   malloc_size is the total size of the block.  If
			   this header is in the middle of a malloc allocation,
			   malloc_size is 0. */
  a_byte_boolean
		trimmed;
			/* TRUE if this block has been trimmed by
			   trim_mem_block. */
} a_mem_block_header;

/*
Entry that precedes each IL entry and indicates some things about it.
*/
typedef struct an_il_entry_prefix *an_il_entry_prefix_ptr;

/*
In the CIR configuration the ECIL writer keeps the id it assigned to an IL
entity in the entity's prefix (a 32-bit field that fits in the padding
that the prefix has on 64-bit hosts).  Other configurations are unchanged.
*/
#if BACK_END_TYPE == BACK_END_TYPE_CIR
#define ECIL_ID_IN_PREFIX TRUE
#else /* BACK_END_TYPE != BACK_END_TYPE_CIR */
#define ECIL_ID_IN_PREFIX FALSE
#endif /* BACK_END_TYPE == BACK_END_TYPE_CIR */

typedef struct an_il_entry_prefix {
#if ENTRY_NUMBER_SHARES_BITS_IN_PREFIX
  /* Note that if you add bits here you must adjust NUM_OF_BIT_FIELDS_IN_PREFIX
     below. */
#endif /* ENTRY_NUMBER_SHARES_BITS_IN_PREFIX */
  a_bit_field	file_scope:1;
			/* TRUE if this IL entry is allocated in the file
			   scope memory region. */
  a_bit_field	secondary_trans_unit:1;
			/* TRUE if this IL entry is in a memory region for
			   a secondary translation unit. */
  a_bit_field	il_walk_flag:1;
			/* Flipped between 0 and 1 to indicate entries that
			   have been visited on a given walk through an IL
			   tree. */
  a_bit_field	il_lowering_flag:1;
			/* Flipped from 0 to 1 by IL lowering to indicate
			   IL entries that have been visited. */
			/* This is not conditional on DO_IL_LOWERING because
			   is it also used by trans_copy.c to mark entries
			   that should be merged into their primary translation
			   unit counterparts. */
#if MAINTAIN_NEEDED_FLAGS
  a_bit_field	keep_in_il:1;
			/* TRUE if the entry should be kept in the IL tree
			   (typically, on one of the lists pointed to from
			   the scope entry).  This is important when the
			   "needed" flag is being maintained, because an
			   entry that is not actually needed must sometimes
			   be retained in the IL tree anyway, for the sake
			   of IL consistency (e.g., a file-scope entity that
			   is declared but never referenced inside a "needed"
			   function). */
#endif /* MAINTAIN_NEEDED_FLAGS */
#if ECIL_ID_IN_PREFIX
  a_bit_field	ecil_inline_specifier:1;
			/* TRUE for a routine whose declaration has the inline
			   specifier (the ECIL writer's INLINE_HINT; set by
			   the front end through ecil_note_inline_specifier).
			   It is here, not in a table of routine addresses,
			   because it goes with the routine's memory region. */
#endif /* ECIL_ID_IN_PREFIX */
#if IL_SHOULD_BE_WRITTEN_TO_FILE
#if ALTERNATE_IL_FILE_FORMAT
  a_bit_field	entry_written:1;
			/* TRUE once the entry has been written to the IL
			   file.  Needed for string entries, for which
			   multiple copies may be written. */
  /* In the alternate file format, each entry has an entry number.  This
     is where it is stored.   Pick a size that makes the whole prefix
     struct the same size as a long.  (This is just for efficiency;
     other sizes will work too.) */
#if ENTRY_NUMBER_SHARES_BITS_IN_PREFIX
  /* The size of the entry_number field is decreased by the bit fields
     defined above (to keep the structure compact). */
/*lint -emacro(506,NUM_OF_BIT_FIELDS_IN_PREFIX)*/
#if ECIL_ID_IN_PREFIX
#define NUM_OF_BIT_FIELDS_IN_PREFIX                                    \
         (6 + ((MAINTAIN_NEEDED_FLAGS != 0)?1:0))
#else /* !ECIL_ID_IN_PREFIX */
#define NUM_OF_BIT_FIELDS_IN_PREFIX                                    \
         (5 + ((MAINTAIN_NEEDED_FLAGS != 0)?1:0))
#endif /* ECIL_ID_IN_PREFIX */
#define BITS_IN_ENTRY_NUMBER                                           \
  (sizeof(an_il_entry_number)*CHAR_BIT - NUM_OF_BIT_FIELDS_IN_PREFIX)
  an_il_entry_number
                entry_number:BITS_IN_ENTRY_NUMBER;
			/* Entry number for the IL entry. */
#else /* !ENTRY_NUMBER_SHARES_BITS_IN_PREFIX */
  /* A full-sized entry_number field is used (wasting some space in each
     prefix). */
  an_il_entry_number
                entry_number;
			/* Entry number for the IL entry. */
#endif /* ENTRY_NUMBER_SHARES_BITS_IN_PREFIX */
#endif /* ALTERNATE_IL_FILE_FORMAT */
#endif /* IL_SHOULD_BE_WRITTEN_TO_FILE */
#if MAINTAIN_ALLOCATION_SEQUENCE_NUMBER
  unsigned long	alloc_seq_number;
			/* For debugging purposes, a sequence number assigned
			   when this block was allocated. */
#endif /* MAINTAIN_ALLOCATION_SEQUENCE_NUMBER */
#if EXPENSIVE_CHECKING
  a_memory_region_number
                file_scope_region_number;
                        /* The memory region holding the file scope IL entries
                           during this IL entry's allocation (this is a proxy
                           value that can be used to identify the translation
                           unit this IL entry was allocated in). */
  a_memory_region_number
                region_number;
                        /* The memory region this IL entry was allocated in. */
  uint32_t      magic_number;
                        /* For debugging purposes, set when an IL entity is
                           allocated to a known value, then tested when
                           accessing the IL prefix (to ensure that the entity
                           is not mistakenly on the stack, in which case it
                           has no prefix). */
#endif /* EXPENSIVE_CHECKING */
#if ECIL_ID_IN_PREFIX
  uint32_t      ecil_id;
			/* The id the ECIL writer assigned to this entity
			   (0 if none): a global id for types, routines,
			   variables and fields; a frame-local id for local
			   variables and labels. */
#endif /* ECIL_ID_IN_PREFIX */
} an_il_entry_prefix;

/*
Macro used by clear_il_entry_prefix to clear the IL lowering flag in
an IL entry prefix only if it exists.
*/
/*
This is not conditional on DO_IL_LOWERING because is it also used
by trans_copy.c to mark entries that should be merged into their
primary translation unit counterparts.
*/
#define clear_il_lowering_flag(epp)                                   \
  (epp->il_lowering_flag = initial_value_for_il_lowering_flag)

/*
Macro used to copy the value of the il_lowering_flag from one IL entity
to another.
*/
#define copy_il_lowering_flag(from_il_entity, to_il_entity)           \
{ il_entry_prefix_of((char *)to_il_entity).il_lowering_flag =         \
                 il_entry_prefix_of((char *)from_il_entity).il_lowering_flag; }

/*
Macro used by clear_il_entry_prefix to clear the keep-in-IL flag in
an IL entry prefix only if it exists.
*/
#if MAINTAIN_NEEDED_FLAGS
#define clear_keep_in_il_flag(epp) (epp->keep_in_il = FALSE)
#else /* !MAINTAIN_NEEDED_FLAGS */
#define clear_keep_in_il_flag(epp) /* Nothing */
#endif /* MAINTAIN_NEEDED_FLAGS */


/*
Macro used by clear_il_entry_prefix to clear the entry_written flag in an
IL entry prefix only if it exists.
*/
#if IL_SHOULD_BE_WRITTEN_TO_FILE && ALTERNATE_IL_FILE_FORMAT
#define clear_entry_written_flag(epp) (epp->entry_written = FALSE)
#else/* !(IL_SHOULD_BE_WRITTEN_TO_FILE && ALTERNATE_IL_FILE_FORMAT) */
#define clear_entry_written_flag(epp) /* Nothing */
#endif /* IL_SHOULD_BE_WRITTEN_TO_FILE && ALTERNATE_IL_FILE_FORMAT */


/*
Macro used by clear_il_entry_prefix to clear the entry number in an
IL entry prefix only if it exists.
*/
#if IL_SHOULD_BE_WRITTEN_TO_FILE && ALTERNATE_IL_FILE_FORMAT
#define clear_il_entry_number(epp) (epp->entry_number = 0)
#else/* !(IL_SHOULD_BE_WRITTEN_TO_FILE && ALTERNATE_IL_FILE_FORMAT) */
#define clear_il_entry_number(epp) /* Nothing */
#endif /* IL_SHOULD_BE_WRITTEN_TO_FILE && ALTERNATE_IL_FILE_FORMAT */

/*
Macro used by clear_il_entry_prefix to clear the ECIL id in an IL entry
prefix only if it exists.  Every IL allocation goes through
clear_il_entry_prefix, so a recycled block never keeps a stale id.
*/
#if ECIL_ID_IN_PREFIX
#define clear_ecil_id(epp)                                            \
  { epp->ecil_id = 0; epp->ecil_inline_specifier = 0; }
#else /* !ECIL_ID_IN_PREFIX */
#define clear_ecil_id(epp) /* Nothing */
#endif /* ECIL_ID_IN_PREFIX */

/*
Macro used by clear_il_entry_prefix to set the allocation sequence
number if it exists.
*/
#if MAINTAIN_ALLOCATION_SEQUENCE_NUMBER
EXTERN_THREAD unsigned long
		allocation_sequence_number_seed;
#define init_alloc_seq_number(epp) \
  ((epp)->alloc_seq_number) = ++allocation_sequence_number_seed;
#else /* !MAINTAIN_ALLOCATION_SEQUENCE_NUMBER */
#define init_alloc_seq_number(epp) /* Nothing */
#endif /* MAINTAIN_ALLOCATION_SEQUENCE_NUMBER */

/*
Macro used by do_alloc and do_fs_alloc to set the memory region number.
This allows strong tracing of memory region problems.
*/
#if EXPENSIVE_CHECKING
#define init_memory_region_metadata(epp, tu_region, region)                  \
  ((((an_il_entry_prefix_ptr)epp)->file_scope_region_number) = (tu_region)); \
  ((((an_il_entry_prefix_ptr)epp)->region_number) = (region))
#else /* !EXPENSIVE_CHECKING */
#define init_memory_region_metadata(epp, tu_region, region) /* Nothing */
#endif /* EXPENSIVE_CHECKING */

/*
Macro used by clear_il_entry_prefix to set the magic number to indicate
that this is indeed an IL entity.
*/
#if EXPENSIVE_CHECKING
#define IL_ENTRY_MAGIC_NUMBER 0xbdbdbdbd  /* Value unlikely to be on stack. */
#define init_magic_number(epp) \
  (((epp)->magic_number) = IL_ENTRY_MAGIC_NUMBER)
#else /* !EXPENSIVE_CHECKING */
#define init_magic_number(epp) /* Nothing */
#endif /* EXPENSIVE_CHECKING */

/*
Initialize an IL entry prefix to default values.  ptr is a pointer (of
any type) to the location containing the prefix.  is_in_file_scope is TRUE if
the entry has been allocated in the file scope memory region, FALSE otherwise.
in_sec_trans_unit is TRUE if the entry has been allocated in a memory
region of a secondary translation unit, FALSE otherwise.  Note that
initialization of the magic number occurs early because subsequent macros 
depend on it being set properly.
*/
#define clear_il_entry_prefix(ptr, is_in_file_scope, in_sec_trans_unit) \
{ an_il_entry_prefix_ptr epp = (an_il_entry_prefix_ptr)ptr;           \
  epp->file_scope = is_in_file_scope;                                 \
  epp->secondary_trans_unit = in_sec_trans_unit;		      \
  epp->il_walk_flag = 0;                                              \
  init_magic_number(epp);                                             \
  clear_il_lowering_flag(epp);                                        \
  clear_keep_in_il_flag(epp);                                         \
  clear_entry_written_flag(epp);                                      \
  clear_il_entry_number(epp);                                         \
  clear_ecil_id(epp)                                                  \
  init_alloc_seq_number(epp);                                         \
}  /* clear_il_entry_prefix */


/* Amount of space to allocate for the prefix.  The size is the smallest
   multiple of HOST_IL_ENTRY_PREFIX_ALIGNMENT that is at least as large as
   the size of an_il_entry_prefix.  This preserves the necessary alignment
   for the entry itself. */
#define SPACE_FOR_IL_ENTRY_PREFIX                                     \
 ((((sizeof(an_il_entry_prefix)-1)/HOST_IL_ENTRY_PREFIX_ALIGNMENT)+1)*       \
  HOST_IL_ENTRY_PREFIX_ALIGNMENT)

#if ECIL_ID_IN_PREFIX
/* The ecil_id field must not grow the prefix of a release build: it fits in
   the padding that rounds the prefix up to the pointer alignment. */
#if !EXPENSIVE_CHECKING && !MAINTAIN_ALLOCATION_SEQUENCE_NUMBER && \
    !IL_SHOULD_BE_WRITTEN_TO_FILE
static_assert(SPACE_FOR_IL_ENTRY_PREFIX == HOST_IL_ENTRY_PREFIX_ALIGNMENT,
              "ecil_id must not enlarge the IL entry prefix");
#endif /* !EXPENSIVE_CHECKING && ... */
#endif /* ECIL_ID_IN_PREFIX */

/* Macros to allow reference to the IL entry prefix that precedes
   the IL entry at ptr.  The usual macro, il_entry_prefix_of, will verify
   that the pointer is indeed an allocated IL entry when EXPENSIVE_CHECKING
   is TRUE.  The il_entry_prefix_of_no_check macro can be used in cases
   where no checking is desired (such as when getting the address of the
   prefix in order to initialize it). */
#if EXPENSIVE_CHECKING
extern an_il_entry_prefix_ptr expensive_il_entry_prefix_of(char *ptr);
#define il_entry_prefix_of(ptr) (*(expensive_il_entry_prefix_of((char *)ptr)))
#define il_entry_prefix_of_no_check(ptr)                              \
  (*(an_il_entry_prefix_ptr)((char *)(ptr) - SPACE_FOR_IL_ENTRY_PREFIX))
#else /* !EXPENSIVE_CHECKING */
#define il_entry_prefix_of(ptr)                                       \
  (*(an_il_entry_prefix_ptr)((char *)(ptr) - SPACE_FOR_IL_ENTRY_PREFIX))
#define il_entry_prefix_of_no_check(ptr) il_entry_prefix_of(ptr)
#endif /* EXPENSIVE_CHECKING */

#if ORPHAN_PROCESSING_NEEDED
/* If orphan processing is needed, each IL entry in the file scope
   memory region is preceded by a next-orphaned-entry pointer (the
   pointer also precedes the an_il_entry_prefix). */
/* Amount of space to allocate for the next-orphaned-entry pointer.
   The size is the smallest multiple of HOST_POINTER_ALIGNMENT that is
   at least as large as the size of a "char *".  This preserves the
   necessary alignment for the entry itself. */
#define SPACE_FOR_FS_ORPHAN_POINTER                                   \
 ((((sizeof(char *)-1)/HOST_POINTER_ALIGNMENT)+1)*                   \
  HOST_POINTER_ALIGNMENT)
/*
Macro to allow reference to the next-orphaned-entry pointer that precedes
the file-scope IL entry at ptr.
*/
#define fs_orphan_pointer_of(ptr)                                     \
  (*(char **)((char *)(ptr) -                                         \
              SPACE_FOR_IL_ENTRY_PREFIX - SPACE_FOR_FS_ORPHAN_POINTER))
#else /* !ORPHAN_PROCESSING_NEEDED */
/* SPACE_FOR_FS_ORPHAN_POINTER is also used to compute the location of the
   copy address pointer (even if no orphan pointers are allocated). */
#define SPACE_FOR_FS_ORPHAN_POINTER 0
#endif /* ORPHAN_PROCESSING_NEEDED */

/* Amount of space to allocate for the translation unit copy address
   pointer that is used when compiling multiple translation units.
   (Such a pointer is used during the process that copies IL from
   secondary translation units to the primary IL).  This is allocated
   for file scope memory regions of secondary translation units.  The
   size is the smallest multiple of HOST_POINTER_ALIGNMENT that is at
   least as large as the size of a "char *".  This preserves the
   necessary alignment for the entry itself. */
#define SPACE_FOR_TRANS_UNIT_COPY_ADDRESS_POINTER                          \
 ((((sizeof(char *)-1)/HOST_POINTER_ALIGNMENT)+1)*                   \
  HOST_POINTER_ALIGNMENT)

/*
Macro to allow reference to the translation unit copy address pointer
that precedes the file-scope IL entry at ptr.

Note that for IL entries that are going to be merged, the
trans_unit_copy_address_of value is overridden with an intermediary value.
To access the true copy address for these IL entries use
transitive_copy_address_of.

See f_transitive_copy_address_of for more information.
*/
#define trans_unit_copy_address_of(ptr)                            \
  (*(char **)((char *)(ptr) -                                         \
              SPACE_FOR_IL_ENTRY_PREFIX -                             \
              SPACE_FOR_FS_ORPHAN_POINTER -                           \
              SPACE_FOR_TRANS_UNIT_COPY_ADDRESS_POINTER))

/*
Checked version of trans_unit_copy_address_of -- when checking code is
on, checks that the pointer is to an entry that does have a copy
address pointer.  Note that this may involve evaluating the argument
more than once.
*/
#if CHECKING
#define checked_trans_unit_copy_address_of(ptr)                    \
  (trans_unit_copy_address_of(                                     \
    ((in_file_scope(ptr) && in_secondary_trans_unit(ptr)) ? (void)0 :   \
       assertion_failed(__FILE__, __LINE__, __EDG_func__, (char *)NULL, \
                        (char *)NULL), \
     ptr)))
#else /* !CHECKING */
#define checked_trans_unit_copy_address_of(ptr)                    \
   trans_unit_copy_address_of(ptr)
#endif /* CHECKING */

/*
Return TRUE if the IL entry pointed to by ptr is in the file scope
memory region.  ptr must point to something allocated in an IL memory
region.
*/
#define in_file_scope(ptr) ((a_boolean)(il_entry_prefix_of(ptr).file_scope))

/*
Return TRUE if the IL entry pointed to by ptr is in a secondary
translation unit.  ptr must point to something allocated in an IL memory
region.
*/
#define in_secondary_trans_unit(ptr) \
  ((a_boolean)(il_entry_prefix_of(ptr).secondary_trans_unit))
			

EXTERN_THREAD a_mem_block_header_ptr
		*mem_region_table;
			/* A dynamically-allocated array.  mem_region_table[i]
			   points to the last memory block header for 
			   region i. */
EXTERN_THREAD a_memory_region_number
		size_of_mem_region_table;
			/* Current size of mem_region_table (number of regions,
			   not number of bytes). */
EXTERN_THREAD a_memory_region_number
		highest_used_region_number;
			/* The highest memory region number used so far. */

EXTERN_THREAD a_memory_region_number
		size_of_function_def_table;
			/* Current size of IL header function_def_table
			   (number of entries, not number of bytes). */
EXTERN_THREAD a_function_def_number
		highest_used_function_def_number;
			/* The highest function definition number used so
			   far. */

/* Conditionally close the "edg" namespace. */
END_EDG_NAMESPACE

#endif /* ifndef MEM_TABLES_H */

