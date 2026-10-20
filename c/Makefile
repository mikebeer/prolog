# Prolog interpreter in portable C89
CC ?= cc
CFLAGS ?= -O2
STD = -std=c89 -pedantic -Wall -Wextra
POSIX = -DPL_POSIX
PYTHON ?= python3
DECKCOLS ?= 71

SRC = src/prolog.c src/p0_base.c src/p1_num.c src/p2_term.c src/p3_lex.c src/p4_db.c \
      src/p5_solve.c src/p5_gc.c src/p6_bi.c src/p7_main.c

prolog: prolog_all.c
	$(CC) $(STD) $(CFLAGS) $(POSIX) -o prolog prolog_all.c -lm

prolog_all.c: $(SRC) tools/amalg.sh
	sh tools/amalg.sh > prolog_all.c

# the card deck for the VM/370 build: every line at most $(DECKCOLS) columns
deck: deck/prolog_deck.c

deck/prolog_deck.c: prolog_all.c tools/deck.py
	mkdir -p deck
	$(PYTHON) tools/deck.py --limit $(DECKCOLS) prolog_all.c > deck/prolog_deck.c
	$(PYTHON) tools/deck.py --verify prolog_all.c deck/prolog_deck.c

# static VM/370 checks of prolog_all.c and the deck (tools/vm370_check.py, PORTING.md)
vm370check: deck
	$(PYTHON) tools/vm370_check.py

deckcheck: deck
	$(PYTHON) tools/deck.py --check --limit $(DECKCOLS) deck/prolog_deck.c
	@echo "deckcheck: ok"

# development sources stay readable: at most 100 columns
linecheck:
	@! awk 'length > 100 { print FILENAME ":" FNR ": " length " columns" }' \
	    src/p[0-9]*.c src/prolog.c tools/*.sh | grep . || (echo "linecheck: lines over 100 columns"; exit 1)
	@echo "linecheck: ok"

test: prolog
	sh tests/run_tests.sh ./prolog

# the whole suite with a garbage collection at every inference (README.md, "Memory")
gcstress: prolog
	PROLOG_GC=stress=1 sh tests/run_tests.sh ./prolog

# build the deck with strict flags and run the whole suite against it
deck-test: deck
	$(CC) $(STD) -pedantic-errors $(CFLAGS) $(POSIX) -o prolog_deck deck/prolog_deck.c -lm
	sh tests/run_tests.sh ./prolog_deck

# strict C89 matrix (gcc, clang, sanitizers)
strict: prolog_all.c deck
	sh tools/strict.sh

bench: prolog
	sh ../bench/run.sh ./prolog

clean:
	rm -f prolog prolog_deck strict_san

.PHONY: deck vm370check deckcheck linecheck test gcstress deck-test strict bench clean
