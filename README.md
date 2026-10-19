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

This is the original version. The copy in the CREXX distribution
(`examples/prolog-in-crexx`) was derived from it and has since diverged.
