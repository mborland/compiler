This directory contains the tools that are used to generate the
$EDG_BASE/src/builtin_defs.h file which describes the signatures of builtin
functions as scraped from suitable GCC and Clang executables.

To avoid any legal issues, the builtin signatures are scraped from GCC and
Clang executables via an available "plugin" interface (rather than looking
at the source code).

There is basically a three-step process that we go through whenever a new
major version of GCC or Clang is released:

1) After we've downloaded and built the latest GCC/Clang executable, we
   invoke the extract-builtin-declarations shell script with the path name
   of the executable.  That script determines whether a GCC or Clang executable
   was specified and then compiles either print-gcc-builtins.cpp or
   print-clang-builtins.cpp as appropriate and executes those programs to
   generate a list of builtin signatures.  Those signatures are placed in
   a "builtins_[Lgm]x_<version>_{a,m}{32,64}.txt" file in the current
   directory.  The filename encodes the compiler (L=Clang, g=GCC, m=Microsoft)
   and version number as well as the size of sizeof(0).

2) With the new file in place, the generate-builtin-table.py script is then
   invoked to coalesce all (or a subset) of the "builtins_*.txt" files
   into the format needed for the builtin_defs.h and builtin_kinds.h files.
   The command-line invocation looks like:

   ./generate-builtin-table.py builtins_*.txt

   Some status information is printed on stderr during the processing.

3) Copy the output from generate-builtin-table to $EDG_BASE/src/builtin_defs.h
   and rebuild.

Notes:
- In their current state, these tools have hard-coded path names that
  correspond to our environment, so they will not work out-of-the-box for you.
  These have to do with the compiler(s) we use to compile the GCC/Clang
  compilers (the same compiler must be used to compile the compiler and the
  program that uses the plugin -- and has changed over time).
- Although the bulk of the processing is devoted to GCC and Clang builtins,
  some Microsoft builtins are also included by manually creating
  builtins_mx_*.txt files.  The format for these follows that of the other
  files.
- This tool requires the GNU parallel tool.
- We've stopped scraping builtins from minor releases of the newest GCC/Clang
  versions (there doesn't appear to be any difference and it just causes the
  tool to run longer).

The generate-libc-builtins.py script is separate from that process.  It reads
the same builtins_*.txt files and writes $EDG_BASE/src/builtin_libc.h, the
list of C library functions (libc and libm) for which a call of __builtin_X can
be translated to a call of X; the CIR back end uses it.  It is run as
   ./generate-libc-builtins.py -o ../../src/builtin_libc.h
and its rule and the names it excludes are described in the script.  It does
not touch builtin_defs.h or builtin_kinds.h.
