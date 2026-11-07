C/C++ Front End Documentation
=================================

.. toctree::
   build
   ext_intf
   int_overview
   il
   lower_il
   symbol_tbl
   lex_pp
   tables
   expr
   decls
   class_decl
   statements
   templates
   exceptions
   pragma
   modules
   trans_unit
   admin
   err_msgs
   includes
   runtime
   utilities
   c_gen_be
   cp_gen_be
   cir_be
   code_style
   testing
   :maxdepth: 3
   :numbered:
   :hidden:

============
Introduction
============

This document describes the function and internal structure of the Edison
Design Group C/C++ Front End.  More precisely, it gives the general overview
necessary as an introduction to the study of the source code of the Front End.
The source code comments provide more detailed information, and that
information is not necessarily repeated here.

The front end accepts the C++ language as defined by the ISO/IEC 14882:2020
standard, as well as a number of the features of the ISO/IEC 14882:2024
standard.  The front end can also be configured to accept earlier revisions of
the C++ standard (e.g., ISO/IEC 14882:2003).  With the proper command-line
options, it alternatively accepts the C language of the ISO/IEC 9899:1990,
ISO/IEC 9899:1999, ISO/IEC 9899:2011, and ISO/IEC 9899:2018 standards, as well
as a number of features from the ISO/IEC 9899:2024 standard.  Certain other
dialects of C++ and C are also accepted, including Microsoft, GNU and Clang
dialects.  C++/CLI, the Microsoft extension to C++ in support of Microsoft's
.NET environment (and formalized through the ECMA-372 standard), is also
implemented.

It is a single pass, including integrated preprocessing.  (Preprocessing
alone -- to a textual output file -- can also be done, as in traditional
compilers with a separate preprocessor.) The syntax-analysis technique is
recursive descent, with a precedence modification to speed up the scanning
of expressions.

The front end does no optimization.  Its goal is to produce a complete and
clean parsed form of the source program, and to diagnose errors.  It does
complete error checking, produces clear error messages (including the position
of the error within the source line), and avoids cascading of errors.  It also
tries to avoid seeming overly finicky to a knowledgeable C or C++ programmer.

The front end is written in a portable dialect of C++, using features from the
ISO/IEC 14882:2011 standard (i.e., it's written using C++11 features).  Host
and target computer characteristics (e.g., command-line interface, character
sets, integer sizes, floating-point representation) are carefully separated
from the main body of code so that they can be easily changed for rehosting or
retargeting.  There is extensive debugging and assertion-checking code, which
can be included or excluded by conditional compilation options.

The output from the front end is a high-level tree-structured in-memory
intermediate language.  This intermediate language contains full information on
declarations, statements, and expressions, all in tree form, and all with
source-correspondence information that simplifies the generation of symbolic
debugging information.

The intermediate language can, if desired, be written to a file, read back from
such a file, or displayed in human-readable form.

When it translates C programs to intermediate language, the front end uses only
the C subset of the intermediate language.  When it translates C++ programs to
intermediate language, it uses the full intermediate language.  However, an
optional IL lowering pass can be used translate the C++ intermediate language
constructs into C intermediate language constructs to reduce the work required
of back ends.

It is expected that the front end will be combined with an appropriate back end
that will generate object code from the intermediate language.  A simple back
end is provided that generates C from the intermediate language, and another
that generates C++ from the intermediate language (the latter is intended for
source-to-source applications).

Also included: minimal runtime, a template prelinker, an IL display utility, a
name demangler, and prototype header files.

This document covers:

* The external interface of the front end -- the command-line options,
  language dialect, intermediate language, and host/target configuration
  issues.  (Error messages are listed in an appendix.)
* The design of the front end -- its philosophy and an overview of the source
  code.

Readers of this document should be familiar with the C++ language, with the C
language, and with the basics of compiler theory.

Indices and tables
==================

* :ref:`genindex`
* :ref:`search`
