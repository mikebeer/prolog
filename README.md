# Prolog in CREXX

A Prolog interpreter written entirely in CREXX (Level B): tokenizer,
operator-precedence parser, unification with a trail, a clause database with
cut-aware SLD resolution, a core of ISO-style built-in predicates, workspace
save/load, and a small CREXX-facing API for running goals from CREXX programs.

    crexx prolog.crexx                  # interactive top level
    crexx prolog.crexx -args family.pl  # consult a file, then the top level

| File | Contents |
|---|---|
| `prolog.crexx` | the interpreter |
| `MANUAL.md`, `MANUAL.docx` | user manual (sample sessions, command reference, index) |
| `PORT_NOTES.md` | implementation notes and CREXX toolchain quirks found on the way |
| `crexxcallback.crexx`, `caller_example.crexx` | calling Prolog from CREXX |
| `family.pl`, `recur.pl` | sample Prolog programs |
| `build_with_prolog.sh` | build helper |
| `crexx-dist/` | the CREXX distribution's copy (`examples/prolog-in-crexx`) |

This is the original version. The copy in the CREXX distribution
(`examples/prolog-in-crexx`) was derived from it and has since diverged; it
is in `crexx-dist/` (with its Prolog source-buffer API and RXPP samples).

## C89 port

`c/` holds a strict C89 port of the interpreter with the same top level and
error messages (see `c/README.md`, and `c/PORTING.md` for VM/370). The shared
regression corpus is in `tests/` (36 programs, `NAME.pl`, `NAME.in`, `NAME.out`);
both versions must pass it:

    sh tests/run_tests_crexx.sh         # CREXX version (CREXX_VM=path/to/crexx)
    cd c && make test                   # C version
    sh bench/run.sh --crexx; sh bench/run.sh c/prolog   # benchmarks, same checksums
    PROLOG_GC_STRESS=1 sh tests/run_tests_crexx.sh      # CREXX, collecting garbage every step

## Memory

The CREXX version reclaims memory while it runs: the solver no longer nests a
CREXX call per goal, and a garbage collector removes dead variable bindings,
trail entries and retracted clauses, so long deterministic loops run in bounded
memory. See `MANUAL.md` section 5.8 (switches `gc/0`, `gc/1`, `statistics/0`,
`)gc`, `PROLOG_GC*` environment variables) and `PORT_NOTES.md` (design,
measurements).
