# P-7: in `--c++26` mode a reference bound to a reference parameter is wrong

A front-end defect report for `main`. It is not a CIR defect: the reference
`cpfe` (the C-generating back end) miscompiles the same source. The CIR
register entry is `security.md` §5, P-7.

Written 2026-11-12. Reproducer:
`P-7-structured-binding-pack-by-reference.cpp` (beside this file,
self-checking, exit status 1 on a wrong result).

## Summary

In `--c++26` mode, a reference whose initializer is the name of a
**reference parameter** is bound to the address of the parameter itself
(the pointer slot), not to the object the parameter refers to. Reads give
garbage and writes go to the wrong place. No diagnostic.

M6.1a found it as "a structured binding pack bound by reference gives wrong
values" (`auto &[...r] = s;`, P1061), which is why the file has that name.
Packs are not needed. The smallest form is:

```cpp
struct T3 { int a; long b; char c; };
long typed_cref(const T3 &s) { const T3 &r = s; return (long)r.a + r.b + r.c; }
```

`--c++17`, `--c++20` and `--c++23` compile it correctly; only `--c++26`
is wrong.

## What is affected (`--c++26`)

| Form | Result |
| --- | --- |
| `const T3 &r = s;` with `s` a `const T3 &` parameter | wrong |
| `auto &r = s;`, `auto &&r = s;`, `decltype(auto) r = (s);` with `s` a reference parameter | wrong |
| `const int &r = i;` with `i` a `const int &` parameter | wrong |
| `const auto &[a, b, c] = s;` with `s` a reference parameter | wrong |
| `const auto &[...refs] = s;`, `auto &[...r] = s;` in a template (reads and writes) | wrong |
| In a template: `const S &s = *p; const auto &[a, b, c] = s;` (a local reference) | wrong |
| Not in a template: `T3 &lr = g; auto &r = lr;` (a local reference) | correct |
| `const auto &r = s;` with `s` an object parameter | correct |
| `const auto &[a, b, c] = *p;` | correct |
| `auto [...v] = s;` (a pack by value) | correct |

## The generated code

The reference `cpfe`, `--c++26`, for `typed_cref` above (`eccp -k`):

```c
long _Z10typed_crefRK2T3( const struct T3 *__4_27_s) {
  auto const struct T3 *__4_42_r = ((const struct T3 *)(&__4_27_s));
  return (((long)(__4_42_r->a)) + (__4_42_r->b)) + ((long)(__4_42_r->c)); }
```

The initializer is `&__4_27_s`, the address of the parameter, cast to the
referent's pointer type. It should be `__4_27_s`. The structured binding
forms show the same thing in the binding container:

```c
auto const struct T3 *__T5 = ((const struct T3 *)(&__4_45_s));
```

So the initializer expression seems to reach the reference-binding code as
an lvalue for the parameter *variable*, without the implicit dereference a
reference-typed entity gets, and only in C++26 mode. Where that happens has
not been looked for; the cast suggests the binding code then forces the
type instead of noticing the mismatch.

## How to reproduce

```sh
eccp --c++26 -o p7 P-7-structured-binding-pack-by-reference.cpp && ./p7
```

On the native macOS build, templates need `--no_auto_instantiation -tused`
(the prelinker does not run there).

Observed (`cpfe` 7.0; two native macOS builds of 2026-11-03 and 2026-11-09
and a Linux x86-64 build, all wrong in the same way):

```
typed_cref     WRONG
auto_ref       WRONG
scalar_cref    WRONG
binding        WRONG
binding_pack   WRONG
local_ref      WRONG
from_object    ok
by_value_pack  ok
from_deref     ok
write_pack     WRONG
```

Expected (`clang++ -std=c++2c`): ten lines of `ok`.

What was and was not checked:

- Run on 2026-11-12 with the two native macOS builds above, and on Linux
  x86-64 with the reference `cpfe` (gcc back end, the day-posture build of
  the `cir` tree at `40c1a4a04a`, which is `main` `cc408045d4` plus the CIR
  work): the same ten lines, the same six wrong. It was not run on `main`'s
  current tip (`ad4cfc4018`) or under CIR.
- M6.1a (2026-11-08) saw the pack form fail on Linux x86-64 in the reference
  configuration and under CIR.
- The non-pack forms were run in `--c++17`, `--c++20`, `--c++23` and
  `--c++26` with the 2026-11-09 build: all correct except `--c++26`.

## For the fix

- A regression test needs a run test in `--c++26` (and an earlier mode as
  the control); per the project's convention one pure `//type:fp` file, plus
  a `-negative` file only if a diagnostic is involved (none is here).
- The fix is ungated front-end code, so on `cir` it changes the reference
  configurations too: the CIR gate then needs the `shared-src` area and
  entries in `tests/.edgy/filters/cir-identity-allowed.txt`
  (`plans/M6.1b-sec1-triage-housekeeping-fuzzing.md`, Phase 0 step 2).
- After the cherry-pick onto `cir`: add a by-reference case to
  `tests/tests/cir/features/cxx/cxx26-structured-binding-packs.sft.cpp`,
  set the row `cxx26.structured_binding_packs` to `yes`, and close P-7 in
  `security.md` §5.
- Worth a look in the same session: why nothing in the test suite caught a
  reference to a reference parameter in C++26 mode. Either no run test does
  that in `--c++26`, or a recording holds the wrong output; neither was
  checked here.
