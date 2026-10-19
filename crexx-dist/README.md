# prolog-in-crexx (the CREXX distribution copy)

This directory is the copy of the interpreter that ships with CREXX, in
[adesutherland/CREXX](https://github.com/adesutherland/CREXX) under
`examples/prolog-in-crexx` (taken at CREXX commit 77ba820c, 3 October 2026).
It was derived from the original in the top directory of this repository and
has since diverged. Kept here next to the original and the C89 port (`../c`).

What it has beyond the original `../prolog.crexx`:

* a Prolog source buffer for CREXX callers: `PrologSourceClear`,
  `PrologSourceAppendString`, `PrologSourceAppendArray` and
  `PrologConsultExecute` (consult the accumulated buffer);
* `prolog_sample.rxpp` and `prolog-rxpp-alternative.md`: Prolog data written
  as RXPP blocks (`##relation …`) in a CREXX program;
* `prolog.rxas`, the compiled assembler of `prolog.crexx` as CREXX ships it.

`MANUAL.*`, `PORT_NOTES.md`, the samples (`family.pl`, `recur.pl`), the
callback examples and `build_with_prolog.sh` are the same as in the top
directory.
