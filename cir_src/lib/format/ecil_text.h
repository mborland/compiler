/*
Part of the EDG Compiler Project, under the Apache License v2.0 with LLVM
Exceptions.
See https://edgcpp.org/LICENSE.txt for license information.
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
*/

/*
ecil_text.h -- the readable text form of an ECIL stream (ecil-dump output
and ecil-asm input).  The form is stable and canonical: dumping then
assembling reproduces the original bytes exactly, because an operand tag
that differs from the shortest encoding is spelled out (u16:5, bin16:"x",
arr16[1], supp16{...}).

Grammar (one record per line; ';' starts a comment):

  ecil MAJOR.MINOR features=0xHHHHHHHH
  frame NAME|0xKK [supp] [flags=0xHH] [reserved=N] [len=N] [bytes=HEX]
    OPNAME operand* [supp{key: value, ...}]
  .raw HEX...       bytes appended to the current frame payload
  .stream HEX...    bytes appended to the stream (ends the current frame)
  !OPNAME operand*  a record without the arity check
  ?0xNN operand*    a record with an arbitrary opcode byte

Operands: nil true false poison; 123 -5 (shortest tag) or u8:5 u16:5 u32:5
u64:5 i8:-5 i16:-5 i32:-5 i64:-5; u128:0xHEX i128:0xHEX; f32:0xHEX
f64:0xHEX; fbits:N:0xHEX; "text" or bin8:"" bin16:"" bin32:"" (escapes \" \\
\xHH); [1, 2] or arr16[...] arr32[...]; `auto` (assembler input only: the
number of frames written so far, for END n_frames).
*/

#ifndef ECIL_TEXT_H
#define ECIL_TEXT_H 1

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "ecil_reader.h"

namespace ecil {

struct a_dump_options {
  bool   check;          /* Validate the stream while dumping. */
  bool   show_offsets;   /* Prefix records with "; @offset". */
  size_t chunk_size;     /* Feed the reader in chunks of this size (0: all). */
  size_t split_at;       /* Feed two chunks split here (0: no split). */

  a_dump_options() : check(true), show_offsets(false), chunk_size(0),
                     split_at(0) {}
};  /* a_dump_options */

bool dump_stream(const uint8_t *data, size_t size,
                 const a_dump_options &options, std::string &out,
                 an_error &error);
/*
Write the text form of the stream to out.  Return false on a stream error
(out then holds the text produced before it).
*/

bool assemble_text(const std::string &text, std::vector<uint8_t> &out,
                   std::string &error);
/* Assemble the text form to bytes.  Return false with a message on error. */

}  /* namespace ecil */

#endif /* ECIL_TEXT_H */
