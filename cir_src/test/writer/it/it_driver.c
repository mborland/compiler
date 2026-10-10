/*
Integration-test driver: replaces the C++-generating back end of a native
cpfe-cp build.  The (debug, macOS) front end keeps all function regions in
memory when run with -d-skip_il_read, so back_end() can hand every region to
the ECIL writer.  Output file: $ECIL_OUT (default "-").
*/
#include "fe_common.h"
#include "ecil_write.h"
#include <stdio.h>
#include <stdlib.h>

char *ecil_output_file_name;
bool ecil_keep_regions;

USING_NAMESPACE_EDG

extern "C++" void cir_be_real_back_end(void);

/* Referenced by il_to_str.c; the real one lives in cp_gen_be.c. */
a_boolean expr_has_comma_operation(an_expr_node_ptr expr)
{
  (void)expr;
  return FALSE;
}

void back_end(void)
{
  const char *out = getenv("ECIL_OUT");
  a_memory_region_number n;

  ecil_output_file_name = (char *)(out != NULL ? out : "-");
  ecil_stream_open();
  for (n = FILE_SCOPE_REGION_NUMBER + 1; n <= highest_used_region_number;
       n++) {
    if (mem_region_table[n] != NULL) ecil_hand_off_region(n);
  }
  ecil_hand_off_region(FILE_SCOPE_REGION_NUMBER);
  ecil_stream_finish();
  ecil_stream_finish_back_end();
  ecil_stream_close();
#if CHECKING
  if (getenv("ECIL_STATS") != NULL) {
    fprintf(stderr, "ecil: code stack high-water %lu\n",
            (unsigned long)ecil_code_stack_high);
  }  /* if */
#endif /* CHECKING */
}
