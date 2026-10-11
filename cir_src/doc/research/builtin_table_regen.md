# The builtin tables do not regenerate from the checked-in data

Status: finding of cir-config, session 2 (2026-10-10), at commit 59a33cffa7.
It is an FE-maintenance item, independent of the CIR work and of how the
libc-flag question (C2) is answered.

## What was done

`dev_tools/builtins/generate-builtin-table.py` was run on all 557 checked-in
`dev_tools/builtins/builtins_*.txt` files, as the README says
(`generate-builtin-table.py builtins_*.txt`, run in a directory that holds the
files or symlinks to them; the script writes `builtin_defs.h` and
`builtin_kinds.h` into the current directory, takes about 3 minutes, and needs
neither GNU parallel nor any compiler). The output was compared with
`src/builtin_defs.h` and `src/builtin_kinds.h`.

## Result

- `builtin_kinds.h`: byte-identical.
- `builtin_defs.h`: **not** identical. `diff` reports 97 lines in about 25
  hunks; the other ~308,700 lines are byte-identical. All differences concern
  four groups of entries:
  - The `__builtin_coro_*` builtins (align, alloc, begin, destroy, done, end,
    frame, free, id, noop, promise, resume, size, suspend). They are placed
    in different tables, with different conditions: the checked-in header
    has them once, in the common table (e.g. `Lx`, `Lx(40000-)`,
    `Lx(160000-)`), the generated one splits them between the common table
    and an architecture table with conditions such as
    `Lx(-159999)Lx(190000-)` and `Lx(190000-)`.
  - `__builtin_thread_pointer` and `__builtin_set_thread_pointer`: the
    checked-in header has the condition `gx8(160200-)` in one table, which
    the generated header lacks.
  - `__builtin___emutls_get_address` and `__builtin___emutls_register_common`:
    checked-in `Sgx(-160199)Sgx4(160200-)`, generated `Sgx`.
  - The condition-string enumerations (`bfci_*`): the generated header has
    `bfci_041c18b8a2` (`Lx(-159999)Lx(190000-)`), `bfci_27f96af2f0`
    (`gx(100000-)Lxmx`) and `bfci_79a7350892`
    (`gx(100000-)Lx(-159999)Lx(190000-)`) that the checked-in one lacks; the
    checked-in one has `bfci_366c0ddb8f` (`gx8(160200-)`) and
    `bfci_4c7f6f17ee` (`Sgx(-160199)Sgx4(160200-)`) that the generated one
    lacks.

## What it is not

- It is not one bad data file. Leaving out each of the six
  `builtins_gx_160200_{a32,a64,m32,m64,r32,r64}.txt` files in turn gives the
  same 97-line diff for a32, a64, and slightly more for m32/m64 and many
  thousand lines for r32/r64 (the RISC-V vector tables depend on them).
- The `gx_160200_*` files are byte-size-identical to the `gx_160100_*` files
  (a32 1619507, a64 2159229, m32 636040, m64 651091, r32 21040891, r64
  21770744 bytes). That suggests they are copies of the 16.1 data, but the
  checked-in header does contain conditions that mention `160200`, which the
  regeneration from these copies cannot produce.
- The coroutine builtins are present in the data of every Clang version from
  15 to 23 (13 or 14 `__builtin_coro_*` entries in each x86-64 file), so the
  difference is in how the generator splits them over its tables, which
  depends on which (compiler, version, architecture, bits) combinations have
  data. The data set is uneven: for example there is no
  `builtins_Lx_150000_a64.txt`, and no `r` files for `gx_150000`/`gx_150001`.
  The cause was not pinned down further.

## Conclusion

The checked-in `src/builtin_defs.h` was generated from a data set (or a
version of the generator) that differs from what is checked in; the
`builtins_gx_160200_*` files in particular cannot be what produced the
conditions that mention GCC 16.2. A manual edit of the header is also possible. Regenerating the header from the checked-in data would change
which builtins exist in some GCC and Clang emulation modes, for every back end.
So nothing was regenerated, and `generate-builtin-table.py` was not changed.

## Suggested follow-up (not CIR work)

1. Find the data (and generator version) the header was generated from and
   check it in, or decide that
   the generator's output is the intended one, and take the resulting changes
   to the FE through the usual review.
2. Add a check that regenerates the header in CI, so the two cannot drift.
3. Make the script less environment-dependent: take an output directory
   argument (it writes into the current directory), and drop the
   `/usr/bin/python3` shebang assumption.
