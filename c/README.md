# Prolog in strict C89

A port of `../prolog.crexx` (the CREXX Prolog interpreter) to strict C89. It
follows the same top level (banner, `?-` prompt, `X = v` answers with the
`more (;)?` prompt, `;` read from standard input), the same error message
texts, the same built-in predicates, the same fixed operator table, and the
same `)listing` / `)clear` / `)lib` / `)help` / `)quit` session commands,
`save/1` and `save_all/1`. Unbound variables print with the same generated
names (`_G12`, `_X_3#17`, `_FA30`) because the renaming counters are kept.

    make              # build ./prolog
    ./prolog          # interactive
    ./prolog file.pl  # consult a file, then the top level
    make test         # run the shared corpus ../tests (33 tests)
    make strict       # gcc + clang -std=c89 -pedantic-errors matrix, sanitizers
    make deck         # 71 column deck/prolog_deck.c (for card-image hosts)
    make vm370check   # external symbols, printf conversions, EBCDIC literals
    make bench        # ../bench programs with timing

## Layout

| Path | Contents |
|---|---|
| `src/p0_base.c` | types, arenas, symbols, text buffers, output, literal character sets |
| `src/p1_num.c` | the CREXX number model: double values printed with 15 digits |
| `src/p2_term.c` | terms, binding trail, unification, standard order, term writer |
| `src/p3_lex.c` | operator table, tokenizer, parser |
| `src/p4_db.c` | clause database, renaming, `listing`, `save` |
| `src/p5_solve.c` | arithmetic and the solver (explicit frame stack, cut barriers) |
| `src/p6_bi.c` | built-in predicates, consult |
| `src/p7_main.c` | bootstrap library, session commands, top level, `main` |
| `prolog_all.c` | the amalgamation of the parts (checked in, built by `tools/amalg.sh`) |
| `tools/` | `amalg.sh`, `strict.sh`, `deck.py`, `vm370_check.py` |
| `tests/run_tests.sh` | runs `../tests/NAME.pl` (stdin `NAME.in`) and compares `NAME.out` |

## Rules kept

* C89: no `//`, no mixed declarations, no `long long`, no `<stdint.h>`.
* Every function is `static` except `main` (one external symbol).
* Character classes are literal sets (`"abc...z"`), never code ranges, so the
  source is EBCDIC safe. A few `'#'`, `'['`, `'\\'` character literals remain;
  `vm370_check.py` reports them as warnings.
* POSIX only under `-DPL_POSIX` (`)lib` listing a directory).
* Clean under `-std=c89 -pedantic -Wall -Wextra` with gcc and clang.

## How it differs from the CREXX version

The CREXX version is a recursive solver over string-encoded terms. The C
version keeps the observable behaviour (answer order, cut semantics including
the odd cases in `tests/33_cutquirks.pl`, the way `if-then-else` is recognised
syntactically, variable names) but uses real term cells, a trail, two arenas
and a non-recursive solver, so it is much faster and does not need the
stack. Differences:

* Arithmetic follows the CREXX number model: all values are IEEE doubles,
  results are printed with 15 significant digits (plain notation for decimal
  exponent -6..17, else `1e+20` style), literals are echoed unchanged. About
  1 result in 100 of an inexact division can differ from CREXX in the 15th
  digit (CREXX's last-digit rounding of such quotients is not plain
  round-to-nearest and could not be reproduced exactly). Integer results,
  `+ - *`, comparisons, `mod`, `//`, `sqrt`, powers agree in the fuzz tests.
* CREXX stops with a conversion panic for numbers outside the double range;
  the C version prints `inf`.
* A missing consult file panics CREXX; C prints
  `prolog: cannot open file: NAME` and the goal fails.
* A quoted atom with a space as functor (`'a b'(1)`) is printed
  as `a(b,1)` by CREXX (its term encoding splits at the space). C prints
  `a b(1)`. Escapes such as `'\n'` crash CREXX but work in C.
* `crexx_call/2,3` is a stub that knows `hello` and `double`.

## Cross-check with SWI-Prolog

The pure-logic programs of the corpus (queens, zebra, primes) give the same
output under SWI-Prolog 9 (`swipl -q -g halt NAME.pl`). The other tests differ
only in the documented dialect points: `write` puts spaces around infix
operators (`1 - 2`), integers are 15-digit doubles (`1.15292150460684e+18`
instead of a bignum), and the error message texts are those of the CREXX
version.
