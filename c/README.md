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
    make test         # run the shared corpus ../tests (34 tests) and tests/c_only
    make gcstress     # the same with a garbage collection at every inference
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
| `src/p5_solve.c` | arithmetic and the solver (explicit frame stack, cut barriers, elided frames) |
| `src/p5_gc.c` | the garbage collector (heap, trail, retracted clauses) |
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

## Sorting and the clause index

* `msort/2` and `sort/2` of a proper list of 32 or more elements are done by a stable
  merge sort in C (`bi_sort_list` in `p6_bi.c`, called from the solver); the library
  insertion sort in `p7_main.c` is quadratic and took more than 13 s at N = 50,000.
  50,000 elements now take 0.4 s, 200,000 take about 1.5 s. Shorter lists, partial
  lists and non-lists still use the library clauses, so answers and the variable
  renaming counters are unchanged for every program that does not sort a long list.
  `sort/2` keeps the last of each run of terms that compare equal, as the library did.
  `predsort/3` and `keysort/2` do not exist (test 14 checks the error).
* First-argument indexing (`p4_db.c`): a predicate with 8 or more clauses called with an
  atom, number or compound first argument is resolved through a hash table
  (atom / numeric value / functor and arity -> the matching clauses plus those with a
  variable first argument, in order). Lookup of asserted facts is O(1) on average
  instead of a scan: 20,000 `assertz(kv(K,V))` followed by 20,000 `kv(K,V)` calls take
  0.2 s (16 s before). `assertz` keeps the index current; `asserta`, `retract` and
  reload drop it and it is rebuilt on the next call. Clauses skipped by the index still
  advance the renaming counter by what trying them would have used, so printed variable
  names (`_G12`, `_X_3#17`) and answer order are identical with and without the index
  (checked on randomly generated programs). `-DIDX_MIN=n` / `-DNSORT_MIN=n` change the thresholds.
* `tests/c_only/big.pl` (C version only): sorts of 50,000 and 200,000 numbers, duplicates,
  and a 100,000-fact table with variable and repeated first arguments.

## Memory and garbage collection

Long-running deterministic programs run in bounded memory: a counting loop
`count(N) :- N > 0, N1 is N - 1, count(N1).` of 10,000,000 steps peaks at 7 MB
(the version before the collector needed about 490 bytes per step: 167 MB for
300,000 steps, 5.5 GB for 10,000,000).

**How it works.**

* *Elided frames* (`p5_solve.c`).  A call whose last candidate clause has been
  entered is no choice point, but its frame used to stay on the stack until the
  call failed or the query ended.  It is now replaced at once by a small record of
  what failing back through it would still do (clear the cut signal, advance the
  renaming counter by the clauses an indexed call skipped, undo trail and heap),
  and records whose clause body has been entered completely are folded into the
  one below, so a tail-recursive loop keeps a constant stack.  "Last candidate"
  also covers clauses that cannot match the first argument (`[]` against
  `[_|_]`, different atoms, numbers or functors; up to 8 remaining clauses, or
  through the first-argument index).  A cut whose target lies below plain call
  frames only is done at once instead of pushing a cut frame.  None of this
  changes answers, their order, side effects or the renaming counters (variable
  names); it was checked against the old engine on thousands of random programs
  with cuts, if-then-else, negation, findall, assert/retract and indexed calls.
* *The collector* (`p5_gc.c`).  The heap is an arena that backtracking releases
  to marks held by the choice points, so the collector keeps allocation order: it
  marks from the roots, copies the live objects in address order into a fresh
  arena and maps every mark to the first live object at or after it.  Every heap
  object has an 8-byte header (size, kind, mark bit) so the heap can be walked.
  Roots: the current goal list, the frames (continuations, goals, findall
  answers, which live in their own arena and may point to outer variables), the
  elided-frame records, the findall template, the query and clause variable maps
  (the `more (;)?` bindings) and the C variables of the callers of the solver,
  registered with `groot()` (the clause being consulted, its marks).  It runs only
  at the top of the solver loop, where nothing else is live.  The trail is weak:
  entries of unreachable variables are dropped and the trail marks remapped.
  Variables passed down a recursion unbound form binding chains (the engine
  binds the caller's variable to the callee's); the collector short-cuts a link
  when no choice point could undo the later binding without the earlier one
  (*variable shunting*), so accumulator loops stay bounded too.
* *Retracted clauses.*  Each clause is now one block of exactly its size.
  Retracted clauses, clause arrays replaced by assert/retract, dropped index
  arrays and the database after `)clear` wait on a list and are freed by the
  collector once no frame can still try them and no live term points into them
  (instantiated bodies share ground sub-terms, numbers and variable names with
  the clause).

**When it runs.**  When the heap has grown past twice what was live after the
last collection (four times when that collection freed less than half; at least
4 MB more, `-DGC_MIN=bytes`), or the retracted storage waiting to be freed has
grown past twice what the last collection kept (at least 4 MB).  Programs that stay below 4 MB of heap never
collect; backtracking still releases memory without a collection.

**Controls.**

| | |
|---|---|
| `garbage_collect/0` | collect now (right after this goal) |
| `statistics/0` | print the collector's counters (collections, time, heap, live, reclaimed, trail, frames, elided frames, freed clauses) |
| `gc(off)`, `gc(on)`, `gc(N)` | switch automatic collection off / on; `gc(N)`: stress mode, collect every N inferences |
| `)gc` | session command: collect and print the statistics |
| `PROLOG_GC=...` | environment variable, comma separated: `off`, `stress=N` (default 1), `verbose` (one line per collection on stderr), `noelide` (keep every frame, as the old engine; for testing) |
| `make gcstress` | the whole suite with `PROLOG_GC=stress=1` (the C-only tests, which hold a lot of live data, collect every 20,000 inferences) |

`gc/1`, `garbage_collect/0` and `statistics/0` are new built-ins of the C version;
`)gc` is not listed by `)help` (its text is part of the shared test corpus).

**Limits.**

* A choice point keeps everything it could backtrack to, as in any Prolog: a loop
  that leaves a choice point per step (a predicate whose remaining clauses cannot
  be ruled out by the first argument) still grows, and non-tail recursion keeps
  its continuation.
* A variable passed down unbound grows a binding chain between collections;
  dereferencing it costs one step per link, so a failure-driven loop such as
  `between(1, N, I), ..., fail` stays slow for large N (as before), though now in
  bounded memory.
* findall/3 keeps its answers in its own arena until it completes; atoms (the
  symbol table) are never freed.
* A collection costs time proportional to the heap; the threshold doubles with
  the live data, so the cost stays a small fraction of the run time.
* Collections happen inside `consult/1` directives too; the outer query's
  continuation is registered as a root.

**One visible change.**  findall/3 used to recognise a variable of an earlier
answer by its address, and a later answer's new variable could reuse that address
after backtracking, so `findall(X, q(X), L)` with `q(_). q(_). q(_).` printed
`[_G142#1,_G142#1,_G142#1]` (one shared variable).  Since the collector moves
objects, variables are now recognised by name, as in the CREXX version (where a
term is its text): `[_G142#1,_G143#2,_G144#3]`, identical to CREXX.

**Measurements** (x86-64, gcc -O2; wall time and peak RSS; "before" is the
commit before the collector, "off" is `PROLOG_GC=off`, frames still elided):

| program | before | now | now, collector off |
|---|---|---|---|
| counting loop, 300,000 steps | 0.67 s, 167 MB | 0.47 s, 6.5 MB | |
| counting loop, 1,000,000 steps | 2.10 s, 553 MB | 1.60 s, 6.5 MB | |
| counting loop, 10,000,000 steps | (about 5.5 GB) | 16.7 s, 6.6 MB | |
| the same with a cut (`cnt/1`), 10,000,000 | (about 5.5 GB) | 17.4 s, 6.5 MB | |
| `tests/c_only/gc_big.pl` | 1635 s, 1168 MB (with `PROLOG_GC=off,noelide`) | 11.5 s, 61 MB | |

The `bench/` programs scaled up (fib(23), 8 queens, nrev loop(30000), primes to
30,000, zebra 300 times, findall over 20,000 and 600x600, assert/retract
100,000):

| bench | before | now | now, collector off |
|---|---|---|---|
| assertloop | 0.84 s, 178 MB | 1.00 s, 113 MB | 0.83 s, 178 MB |
| fib | 0.18 s, 68 MB | 0.26 s, 61 MB | 0.19 s, 72 MB |
| findall_big | 3.99 s, 34 MB | 3.79 s, 28 MB | 3.89 s, 29 MB |
| nqueens | 0.55 s, 2.3 MB | 0.58 s, 2.4 MB | 0.61 s, 2.4 MB |
| nrev | 7.60 s, 5969 MB | 4.35 s, 6.6 MB | 6.70 s, 3815 MB |
| primes | 12.0 s, 30 MB | 4.6 s, 21 MB | 10.4 s, 23 MB |
| zebra | 1.40 s, 2.4 MB | 1.64 s, 2.4 MB | 1.54 s, 2.4 MB |

Programs below 4 MB of heap never collect; what they pay is the bookkeeping of
elided frames and the clause look-ahead (nqueens +5%, zebra, a pure
backtracking search, +10 to 17%).  fib keeps everything alive through choice
points, so its collections copy without freeing much (+40%, 3 collections).
primes gets faster because shunting shortens the long binding chains it walks.

## Cross-check with SWI-Prolog

The pure-logic programs of the corpus (queens, zebra, primes) give the same
output under SWI-Prolog 9 (`swipl -q -g halt NAME.pl`). The other tests differ
only in the documented dialect points: `write` puts spaces around infix
operators (`1 - 2`), integers are 15-digit doubles (`1.15292150460684e+18`
instead of a bignum), and the error message texts are those of the CREXX
version.
