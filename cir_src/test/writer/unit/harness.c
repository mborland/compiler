#include "fe_common.h"
#include "ecil_write.h"
#include "cir_be.h"
#include <stdio.h>
#include <stdlib.h>
USING_NAMESPACE_EDG
extern void back_end(void);
/*
The reason of an unsupported construct is copied into stream-owned storage:
a caller's buffer (the old static buffers of the writer) may be overwritten
while the first reason is kept; a reason taken aside and put back survives
reasons noted in between.  Returns the number of failures.
*/
static int check_reasons(void) {
  char buf[64];
  int failures = 0;
  an_ecil_saved_reason saved;

  ecil_unsupported_reason = NULL;
  snprintf(buf, sizeof(buf), "the operator %d", 5);
  ecil_note_unsupported(buf);
  snprintf(buf, sizeof(buf), "the expression kind %d", 9);
  ecil_note_unsupported(buf);          /* Ignored: the first reason wins. */
  snprintf(buf, sizeof(buf), "overwritten");
  if (ecil_unsupported_reason == NULL ||
      strcmp(ecil_unsupported_reason, "the operator 5") != 0) {
    fprintf(stderr, "FAIL first reason: %s\n",
            ecil_unsupported_reason ? ecil_unsupported_reason : "(none)");
    failures++;
  }
  ecil_save_reason(&saved);
  if (ecil_unsupported_reason != NULL) {
    fprintf(stderr, "FAIL save did not clear\n");
    failures++;
  }
  ecil_note_unsupported("a reason of the callee");
  ecil_restore_reason(&saved);
  if (ecil_unsupported_reason == NULL ||
      strcmp(ecil_unsupported_reason, "the operator 5") != 0) {
    fprintf(stderr, "FAIL restore: %s\n",
            ecil_unsupported_reason ? ecil_unsupported_reason : "(none)");
    failures++;
  }
  ecil_unsupported_reason = NULL;
  ecil_save_reason(&saved);            /* Nothing to save... */
  ecil_note_unsupported("later");
  ecil_restore_reason(&saved);         /* ...so the later reason stays. */
  if (ecil_unsupported_reason == NULL ||
      strcmp(ecil_unsupported_reason, "later") != 0) {
    fprintf(stderr, "FAIL restore of nothing\n");
    failures++;
  }
  ecil_unsupported_reason = NULL;
  /* A long reason is cut at ECIL_REASON_MAX - 1 bytes, backing up so that a
     UTF-8 sequence is not split (U+00E9 is the two bytes C3 A9). */
  {
    char  longer[400];
    size_t  length;

    memset(longer, 'a', sizeof(longer));
    longer[sizeof(longer) - 1] = '\0';
    ecil_note_unsupported(longer);
    length = strlen(ecil_unsupported_reason);
    if (length != ECIL_REASON_MAX - 1) {
      fprintf(stderr, "FAIL long reason: %zu bytes\n", length);
      failures++;
    }
    ecil_unsupported_reason = NULL;
    memset(longer, 'a', sizeof(longer));
    longer[254] = (char)0xC3;
    longer[255] = (char)0xA9;
    longer[sizeof(longer) - 1] = '\0';
    ecil_note_unsupported(longer);
    length = strlen(ecil_unsupported_reason);
    if (length != 254) {
      fprintf(stderr, "FAIL multibyte cut: %zu bytes\n", length);
      failures++;
    }
    ecil_unsupported_reason = NULL;
    /* Invalid UTF-8 (an identifier in another encoding) is replaced, so the
       optional supp string cannot make the stream invalid. */
    ecil_note_unsupported("a name \xFF\xC3 and \xE2\x82 and \xED\xA0\x80"
                          " and \xC3\xA9 ok");
    if (strcmp(ecil_unsupported_reason,
               "a name ?? and ?? and ??? and \xC3\xA9 ok") != 0) {
      fprintf(stderr, "FAIL invalid UTF-8: %s\n", ecil_unsupported_reason);
      failures++;
    }
    ecil_unsupported_reason = NULL;
    if (ecil_utf8_prefix_length("abc", 255) != 3 ||
        ecil_utf8_prefix_length("ab\xE2\x82\xAC", 4) != 2 ||
        ecil_utf8_prefix_length("ab\xE2\x82\xAC", 5) != 5 ||
        ecil_utf8_prefix_length("ab\xE2\x82\xAC", 3) != 2 ||
        ecil_utf8_prefix_length("ab\xE2\x82\xAC", 2) != 2 ||
        ecil_utf8_prefix_length("", 0) != 0) {
      fprintf(stderr, "FAIL utf8 prefix length\n");
      failures++;
    }
  }
  return failures;
}

int main(int argc, char **argv) {
  ecil_output_file_name = argc > 1 ? argv[1] : (char *)"-";
  targ_sizeof_pointer = 8; targ_sizeof_int = 4; targ_alignof_int = 4;
  targ_sizeof_long = 8; targ_sizeof_double = 8; targ_alignof_double = 8;
  targ_char_bit = 8; targ_little_endian = TRUE;
  targ_has_signed_chars = TRUE;
  std_version = 201112; C_dialect = C_dialect_ANSI;
  primary_source_file_name = (char *)"t.c";
  int mode = argc > 2 ? atoi(argv[2]) : 0;
  if (mode == 2) {
    int failures = check_reasons();

    fprintf(stderr, failures == 0 ? "reasons: ok\n" : "reasons: FAILED\n");
    return failures != 0;
  }
  ecil_stream_open();
  if (mode == 1) ecil_abort();
  ecil_hand_off_region(FILE_SCOPE_REGION_NUMBER);
  ecil_stream_finish();
  back_end();
  ecil_stream_close();
  return 0;
}
