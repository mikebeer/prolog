:- X = "hello", writeln(X), atom_codes(A, X), writeln(A).
:- X = "", writeln(X).
:- X = "a b", length(X, N), writeln(N).
:- "abc" = [A|T], writeln(A), atom_codes(R, T), writeln(R).
:- atom_codes(abc, C), C = "abc", writeln(same).
str_len(S, N) :- length(S, N).
:- str_len("twelve chars", N), writeln(N).
:- X = "it's", atom_codes(A, X), print(A), nl.
:- X = 'say "hi"', write(X), nl, print(X), nl.
:- X = "say ""hi""", atom_codes(A, X), writeln(A).
upper_codes([], []).
upper_codes([C|Cs], [U|Us]) :- ( C >= 97, C =< 122 -> U is C - 32 ; U = C ), upper_codes(Cs, Us).
:- atom_codes(hello, Cs), upper_codes(Cs, Us), atom_codes(R, Us), writeln(R).
:- atom_chars(X, [h, i]), atom_length(X, N), writeln(X/N).
:- writeln(done).
