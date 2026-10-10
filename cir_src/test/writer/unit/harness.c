#include "fe_common.h"
#include "ecil_write.h"
#include "cir_be.h"
#include <stdio.h>
#include <stdlib.h>
USING_NAMESPACE_EDG
extern void back_end(void);
int main(int argc, char **argv) {
  ecil_output_file_name = argc > 1 ? argv[1] : (char *)"-";
  targ_sizeof_pointer = 8; targ_sizeof_int = 4; targ_alignof_int = 4;
  targ_sizeof_long = 8; targ_char_bit = 8; targ_little_endian = TRUE;
  targ_has_signed_chars = TRUE;
  std_version = 201112; C_dialect = C_dialect_ANSI;
  primary_source_file_name = (char *)"t.c";
  int mode = argc > 2 ? atoi(argv[2]) : 0;
  ecil_stream_open();
  if (mode == 1) ecil_abort();
  ecil_hand_off_region(FILE_SCOPE_REGION_NUMBER);
  ecil_stream_finish();
  back_end();
  ecil_stream_close();
  return 0;
}
