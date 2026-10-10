/*
Part of the EDG Compiler Project, under the Apache License v2.0 with LLVM
Exceptions.
See https://edgcpp.org/LICENSE.txt for license information.
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
*/

/*

host_util.h -- host environment utility routines that are shared by
               the front end and other utility programs.

*/

/* Avoid including these declarations more than once. */
#ifndef HOST_UTIL_H
#define HOST_UTIL_H 1

/* Conditionally open the "edg" namespace. */
BEGIN_EDG_NAMESPACE

unsigned long crc_32(a_const_char	*str,
		     unsigned long	prev_crc)
/*
Determines and returns the CRC-32 value for a null-terminated string.
This is the CRC used by ZMODEM and PKZIP.  There are plenty of more
efficient ways of computing CRC; this straightforward approach is used
only because in this context the CRC is needed just a small number of times.
prev_crc is a previously computed value.  This permits a CRC
to be computed by several calls to this routine.  If there is no previous
value, a zero should be passed in.
*/
{
  unsigned long crc;

  /* Start with an initial value of 0xfffffff, or undo the exclusive
     or done when the previous value was returned. */
  crc = prev_crc ^ 0xffffffff;
  while (*str != '\0') {
    unsigned long ch = (unsigned char)*str++;
    int nbit;

    for (nbit = 0; nbit < CHAR_BIT; nbit++, ch >>= 1) {
      int low_bit = (ch^crc) & 1;
      crc >>= 1;
      if (low_bit) crc ^= 0xEDB88320L;
    }  /* for */
  }  /* while */
  crc ^= 0xffffffff;
  return crc;
}  /* crc_32 */

#if ONE_INSTANTIATION_PER_OBJECT

a_const_char *generate_instantiation_output_file_name(
                                                    a_const_char *mangled_name)
/*
Generate the name of an instantiation output file that is used in
one instantiation per object mode.  A pointer to a static buffer
is returned, so the value must be copied before this routine is
called again.
*/
{
#define MAX_INSTANTIATION_OUTPUT_FILE_LEN 31
  STATIC_THREAD char buffer[MAX_INSTANTIATION_OUTPUT_FILE_LEN+1];
  long long          max_len_without_suffix;

  /* Determine the output file name.  Use the mangled name (or the beginning
     of it) plus an underscore plus the hexadecimal for the CRC-32 checksum
     for the whole mangled name.  Note that the following computation uses
     the size of GEN_C_FILE_SUFFIX or OBJECT_FILE_SUFFIX (which includes the
     null terminator), not the strlen. */
  max_len_without_suffix = MAX_INSTANTIATION_OUTPUT_FILE_LEN - 8;
#if BACK_END_HAS_OUTPUT_FILE
  max_len_without_suffix -= (long long)sizeof(GEN_C_FILE_SUFFIX);
#else /* !BACK_END_HAS_OUTPUT_FILE */
  max_len_without_suffix -= (long long)sizeof(OBJECT_FILE_SUFFIX);
#endif /* BACK_END_HAS_OUTPUT_FILE */
  check_assertion(max_len_without_suffix > 0);
  (void)strncpy(buffer, mangled_name, size_t_arg(max_len_without_suffix));
  buffer[max_len_without_suffix] = '\0';

  unsigned long crc_value = crc_32(mangled_name, (unsigned long)0);
  size_t        used_buffer_len = strlen(buffer);
  size_t        remaining_buffer_len =
                   ((MAX_INSTANTIATION_OUTPUT_FILE_LEN + 1) - used_buffer_len);
  LOCAL_UNUSED int
                chars_written = detail::snprintf_impl(buffer + used_buffer_len,
                                                      remaining_buffer_len,
                                                      "_%08lx", crc_value);
  /* If this assertion fails, there was an error writing the string. */
  check_assertion(chars_written > 0);
#undef MAX_INSTANTIATION_OUTPUT_FILE_LEN
  return buffer;
}  /* generate_instantiation_output_file_name */

#endif /* ONE_INSTANTIATION_PER_OBJECT */

#if EDG_WIN32 && UNICODE_SOURCE_SUPPORTED 
#if defined(MEM_MANAGE_H)  /* Will be FALSE when building the prelinker. */
static wchar_t *conv_utf8_to_wchar_full(a_const_char *char_buffer, 
                                        a_boolean    *utf8_character_seen)
/*
Copy the supplied char_buffer to a buffer as wchar_t characters, translating
any UTF-8 multibyte characters to UTF-16, and return the address of the
buffer.  utf8_character_seen is set to TRUE if any utf8 character is detected. 
*/
{
  unsigned char   *p;
  unsigned long   unicode_char;
  a_boolean       err;
  int             num_utf8_bytes;
  int             num_utf16_chars;
  sizeof_t        utf16_len;
  unsigned short  utf16_chars[2];
  int             i;
  static wchar_t  *buffer;
  static sizeof_t buffer_allocation_size = 512 * sizeof(wchar_t);

  if (utf8_character_seen != NULL) *utf8_character_seen = FALSE;
/* Macro to add one wide character to the buffer, expanding the buffer as
   needed. */
#if !defined(MEM_MANAGE_H) || STANDALONE_UTILITY_PROGRAM
/* The memory management environment upon which the text buffer utility
   relies is not available in a standalone utility program, so we must
   provide the facility locally. */
#define add_to_wchar_buffer(wchar)                                \
  if ((++utf16_len) * sizeof(wchar_t) > buffer_allocation_size) { \
    buffer_allocation_size *= 2;                                  \
    buffer = (wchar_t *)realloc((a_stdio_arg)buffer,              \
                                buffer_allocation_size);          \
    if (buffer == NULL) {                                         \
      fprintf(stderr, "Out of memory.\n");                        \
      exit(RC_ERROR);                                             \
    }  /* if */                                                   \
  }  /* if */                                                     \
  buffer[utf16_len-1] = wchar;
#else /* !(!defined(MEM_MANAGE_H) || STANDALONE_UTILITY_PROGRAM) */
#define add_to_wchar_buffer(wchar)                                  \
  utf16_len++;                                                      \
  ensure_text_buffer_space(wchar_translation_buffer,                \
                           (utf16_len) * sizeof(wchar_t));          \
  /* Update the buffer pointer to reflect possible reallocation. */ \
  buffer = (wchar_t *)wchar_translation_buffer->buffer;             \
  buffer[utf16_len-1] = wchar;
#endif /* !defined(MEM_MANAGE_H) || STANDALONE_UTILITY_PROGRAM */

#if !defined(MEM_MANAGE_H) || STANDALONE_UTILITY_PROGRAM
  if (buffer == NULL) {
    buffer = (wchar_t *)malloc(buffer_allocation_size);
    if (buffer == NULL) {
      fprintf(stderr, "Out of memory.\n");
      exit(RC_ERROR);
    }  /* if */
  }  /* if */
#else /* !(!defined(MEM_MANAGE_H) || STANDALONE_UTILITY_PROGRAM) */
  if (wchar_translation_buffer == NULL) {
    wchar_translation_buffer = alloc_text_buffer(buffer_allocation_size);
    buffer = (wchar_t *)wchar_translation_buffer->buffer;
  }  /* if */
#endif /* !defined(MEM_MANAGE_H) || STANDALONE_UTILITY_PROGRAM */
  utf16_len = 0;
  for (p = (unsigned char *)char_buffer; *p != 0; p += num_utf8_bytes) {
    if (*p < 0x80) {
      /* This is an ASCII character, so we can just copy it directly. */
      add_to_wchar_buffer(*p);
      num_utf8_bytes = 1;
    } else {
      /* Convert a UTF-8 character to a single Unicode code point, noting
         how many bytes from char_buffer were occupied by the UTF-8
         representation. */
      if (utf8_character_seen != NULL) *utf8_character_seen = TRUE;
      num_utf8_bytes = mbc_to_wide_char((char *)p, &unicode_char, &err,
                                        /*is_native=*/FALSE);
      /* Convert that to either one UTF-16 value or a pair of surrogates. */
      num_utf16_chars = ucn_to_utf16(unicode_char, utf16_chars);
      check_assertion(num_utf16_chars <= 2);
      /* Copy the result into the buffer. */
      for (i = 0; i < num_utf16_chars; ++i) {
        add_to_wchar_buffer(utf16_chars[i]);
      }  /* for */
    }  /* if */
  }  /* for */
  /* Add the terminating null character. */
  add_to_wchar_buffer(0);
  return buffer;
#undef add_to_wchar_buffer
}  /* conv_utf8_to_wchar_full */


wchar_t *translate_filename_to_wchar(a_const_char *filename)
/*
Copy the supplied filename to a buffer as wchar_t characters, translating
any UTF-8 multibyte characters to UTF-16, and return the address of the
buffer.  If the filename contains only ASCII characters, the returned
address will be NULL, indicating that the filename needs no translation and
can be used directly.  The buffer is reused by each successive call, so the
caller should copy the contents as needed.
*/
{
  wchar_t   *buffer; 
  a_boolean utf8_character_seen;

  buffer = conv_utf8_to_wchar_full(filename, &utf8_character_seen);
  return utf8_character_seen ? buffer : (wchar_t *)NULL;
}  /* translate_filename_to_wchar */


wchar_t *conv_utf8_to_wchar(a_const_char *buffer)
/* 
Copy the supplied buffer containing UTF-8 to a buffer as wchar_t
characters, translating any UTF-8 multibyte characters to UTF-16, and
return the address of the buffer. The buffer is reused by each
successive call, so the caller should copy the contents as needed.
*/
{
  return conv_utf8_to_wchar_full(buffer, /*utf8_character_seen=*/NULL);
}  /* conv_utf8_to_wchar */
#endif /* defined(MEM_MANAGE_H) */
#endif /* EDG_WIN32 && UNICODE_SOURCE_SUPPORTED */


a_boolean get_file_modification_time(a_const_char *file_name,
                                     time_t       *p_time)
/*
Determine whether a file exists, and if so, return the last modification
time.  Return TRUE if the file exists and is a regular file, FALSE otherwise.
*/
{
  a_boolean	is_regular = FALSE;
#if defined(MEM_MANAGE_H)  /* Will be FALSE when building the prelinker. */
  a_boolean	encoding_change_needed = TRUE;
#endif /* defined(MEM_MANAGE_H) */

#if defined(MEM_MANAGE_H)  /* Will be FALSE when building the prelinker. */
#if EDG_WIN32 && UNICODE_SOURCE_SUPPORTED
  wchar_t *wchar_file_name = translate_filename_to_wchar(file_name);
  encoding_change_needed = FALSE;
  if (wchar_file_name != NULL) {
    /* Use the Windows _wstat function instead of regular stat to handle
       non-ASCII characters in the file name. */
    struct _stat buf;
    if (_wstat(wchar_file_name, &buf) == 0) {
      is_regular = ((buf.st_mode & S_IFREG) != 0);
      if (is_regular && p_time != NULL) *p_time = buf.st_mtime;
    } else {
      /* If the file doesn't exist, set the time to zero just to be neat. */
      if (p_time != NULL) *p_time = 0;
    }  /* if */
  } else
#endif /* EDG_WIN32 && UNICODE_SOURCE_SUPPORTED */
#endif /* defined(MEM_MANAGE_H) */
  /* Do not insert code here. */
  {
    /* Check the file type.  Use the stat call instead of fstat because some
       implementations do not have the _file field in the structure. */
    struct stat buf;
#if defined(MEM_MANAGE_H)  /* Will be FALSE when building the prelinker. */
    /* Translate the file name into the form used by the file system. */
    if (encoding_change_needed) {
      /* Skip the translation if we verified above that no non-ASCII
         characters were present. */
      file_name = file_name_in_external_encoding(file_name);
    }  /* if */
#endif /* defined(MEM_MANAGE_H) */
    if (stat(file_name, &buf) == 0) {
      /* Use the POSIX S_ISREG if it is defined.  Otherwise use the
         non-POSIX test using S_IFREG. */
#ifdef S_ISREG
      is_regular = S_ISREG(buf.st_mode);
#else /* ifndef S_ISREG */
      is_regular = ((buf.st_mode & S_IFREG) != 0);
#endif /* ifdef S_ISREG */
      if (is_regular && p_time != NULL) *p_time = buf.st_mtime;
    } else {
      /* If the file doesn't exist, set the time to zero just to be neat. */
      if (p_time != NULL) *p_time = 0;
    }  /* if */
  }
  return is_regular;
}  /* get_file_modification_time */

/* Conditionally close the "edg" namespace. */
END_EDG_NAMESPACE

#endif /* ifndef HOST_UTIL_H */

