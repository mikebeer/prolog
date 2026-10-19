# Porting notes: VM/370 (and other small hosts)

* `long` may be 32 bits. All counters and arena offsets are `long`; terms
  are never stored in an `int`. Nothing assumes 64-bit integers. Big
  integers are not implemented, as in the CREXX version: numbers are doubles
  printed with 15 digits, so `fact(25)` prints `1.5511210043331e+25`.
* Characters: classification uses literal sets (`S_LOWER`, `S_UPPER`,
  `S_DIGIT`, `S_SYM`, `S_SOLO` in `src/p0_base.c`), no `isalpha` or
  `'a'..'z'` ranges, so EBCDIC code pages work. The control tags of the
  CREXX version (bytes 2 to 7) are not used; terms are cells.
* Build the 71 column deck: `make deck` writes `deck/prolog_deck.c` and
  verifies that joining the continuation lines gives back `prolog_all.c`.
  `make deckcheck` and `make vm370check` check the width, the external
  symbols (only `main`), `printf` conversions and EBCDIC-sensitive literals.
* No POSIX: compile without `-DPL_POSIX`. `)lib` then reports that listing
  a directory is not supported. Input is read with `fgets` from stdin,
  files with `fopen`; both are standard C.
* Memory: two arenas allocated in 256 KB blocks (`BLKSZ` in `p0_base.c`);
  reduce it for a small region. The heap is released on backtracking, so a
  long failure-driven loop runs in constant space.
* Printing doubles uses `sprintf("%.40e")` and rounds the digit string
  itself; a C library without exact `%e` output may differ in the last digit.
* Known differences from CREXX are listed in `README.md`.
