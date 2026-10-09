/*
Part of the EDG Compiler Project, under the Apache License v2.0 with LLVM
Exceptions.
See https://edgcpp.org/LICENSE.txt for license information.
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
*/

/* Utility program to check lang_feat.h, host_envir.h, and targ_def.h for
   #defines that should be but are not in the --dump_configuration output.
   Because each of those headers defines macros that are for local use,
   are derived from other macro values, or otherwise are not intended to
   be set in defines.h, a list of exceptions that will not be printed as
   missing macros can be provided.

   Initial version: wmm, 2010-10-31.
*/

#include <string>
using std::string;
#include <set>
using std::set;
#include <cstdio>
#include <cstdlib>
#include <cstring>

bool error_seen = false;

/*****************************************************************************/

FILE* open_file(const string& filename) {
  FILE* fp;
  if (!(fp = fopen(filename.c_str(), "r"))) {
    fprintf(stderr, "Unable to open %s\n.", filename.c_str());
    exit(999);
  }
  return fp;
}  /* open_file */

/*****************************************************************************
 * Return the name of the next macro definition found in the specified file  *
 * or "" if EOF is reached.  If expect_comments is true, the file is the     *
 * output of the --dump_configuration command-line option, and macro names   *
 * can appear as comments as well as in #define directives.                  *
 *****************************************************************************/

string get_next_macro_name(FILE* fp, bool expect_comments) {
  char buff[256];
  while (!feof(fp)) {
    if (fgets(buff, 256, fp)) {
      if (memcmp(buff, "#define ", 8) == 0 ||
          (expect_comments && memcmp(buff, "/*      ", 8) == 0)) {
        int i = strspn(buff + 8, "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ_");
        if (buff[8 + i] == ' ') {
          return string(buff + 8, i);
        }
      }  /* if (memcmp...) */
    }  /* if (fgets...) */
  }  /* while (!feof...) */
  return "";
}  /* get_next_macro_name */

/*****************************************************************************
 * Open the specified file and add an entry to the specified set for each    *
 * macro name that is found.  If expect_comments is true, the file is the    *
 * output of the --dump_configuration command-line option, and macro names   *
 * can appear as comments as well as in #define directives.                  *
 *****************************************************************************/

void populate_set(set<string>& the_set,
                  string filename,
                  bool expect_comments) {
  FILE* fp = open_file(filename);
  string macro_name;
  string last_name;
  while ((macro_name = get_next_macro_name(fp, expect_comments)).size() > 0) {
    if (expect_comments) {
      /* Check to make sure the configuration file is in lexical order. */
      if (macro_name < last_name &&
          macro_name != string("LEGACY_TARGET_CONFIGURATION_NAME")) {
        printf("Out of order: %s\n     precedes %s.\n", last_name.c_str(),
               macro_name.c_str());
        error_seen = true;
      }  /* if (macro_name >= last_name) */
      last_name = macro_name;
    }  /* if (expect_comments) */
    the_set.insert(macro_name);
  }  /* while ((macro_name...)) */
  fclose(fp);
}  /* populate_set */

/*****************************************************************************
 * Open the specified file, find all the #defines in it, and report an error *
 * if a macro name does not occur in either config_output or exceptions.     *
 *****************************************************************************/

void check_file(const char* path,
                const char* filename,
                const set<string>& config_output,
                const set<string>& exceptions) {
  FILE* fp = open_file(string(path) + "/" + filename);
  string macro_name;
  static set<string> reported;
  while ((macro_name = get_next_macro_name(fp, false)).size() > 0) {
    /* back_end_type.h defines its own include guard and the BACK_END_TYPE_*
       enumerators; they are not configuration macros. */
    bool is_enumerator = (macro_name == "BACK_END_TYPE_H" ||
                          macro_name.compare(0, 14, "BACK_END_TYPE_") == 0);
    if (!is_enumerator &&
        config_output.find(macro_name) == config_output.end() &&
        exceptions.find(macro_name) == exceptions.end()) {
      if (reported.find(macro_name) == reported.end()) {
        printf("%s: %s not found.\n", filename, macro_name.c_str());
        reported.insert(macro_name);
        error_seen = true;
      }  /* if (reported.find...) */
    }  /* if (config_output.find...) */
  }  /* while ((macro_name...)) */
  fclose(fp);
}  /* check_file */

/*****************************************************************************/

int main(int argc,
         char* argv[]) {
  set<string> config_output;
  set<string> exceptions;

  if (argc != 4) {
    printf("Usage: check_defines config_output exceptions header_path\n");
    exit(998);
  }

  populate_set(config_output, argv[1], true);
  populate_set(exceptions, argv[2], false);

  check_file(argv[3], "back_end_type.h", config_output, exceptions);
  check_file(argv[3], "lang_feat.h", config_output, exceptions);
  check_file(argv[3], "host_envir.h", config_output, exceptions);
  check_file(argv[3], "targ_def.h", config_output, exceptions);

  if (!error_seen) {
    printf("All #defines accounted for.\n");
  }

  exit((int)error_seen);

}  /* main */
