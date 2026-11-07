================
ClangIR back end
================

The ClangIR (CIR) back end translates the front end's intermediate language
to ClangIR through a streaming, versioned interchange format, so that
programs can be compiled with the LLVM and MLIR code generator instead of
through C.  This chapter will describe its design and use; for now it lists
what the back end supports.

Support status
==============

Each table below covers one language standard or feature family.  The CIR
column always carries the status as text, in addition to its color.

.. include:: cir_support.inc
