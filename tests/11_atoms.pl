:- atom_codes(abc, L), writeln(L).
:- atom_codes(A, [104, 105]), writeln(A).
:- atom_chars(hello, L), writeln(L).
:- atom_chars(A, [w, o, r, l, d]), writeln(A).
:- atom_length(hello, N), writeln(N).
:- atom_length('hello world', N), writeln(N).
:- atom_length(12345, N), writeln(N).
:- atom_concat(abc, def, X), writeln(X).
:- atom_concat(abc, 123, X), writeln(X).
:- atom_concat('hello ', world, X), writeln(X), print(X), nl.
:- char_code(a, X), writeln(X).
:- char_code(C, 66), writeln(C).
:- upcase_atom('hello World', U), writeln(U).
:- downcase_atom('Hello WORLD', D), writeln(D).
:- number_codes(N, "42"), Y is N + 1, writeln(Y).
:- number_chars(N, ['1', '2', '.', '5']), writeln(N).
:- atom_codes(12, L), writeln(L).
:- atom_chars(X, ['1', '2']), writeln(X), ( integer(X) -> writeln(int) ; writeln(atom) ).
:- X = "abc", writeln(X).
:- atom_codes(A, "xyz"), writeln(A).
:- atom(abc), \+ atom("abc"), writeln(string_is_list).
:- atom_concat(X, abc, abcabc) -> writeln(X) ; writeln(failed).
:- atom_chars(abc, Cs), reverse(Cs, Rs), atom_chars(R, Rs), writeln(R).
:- upcase_atom(abc, U), atom_length(U, N), writeln(U/N).
:- atom_codes(abc, Cs), maplist(succ_code, Cs, Ds), atom_codes(R, Ds), writeln(R).
succ_code(X, Y) :- Y is X + 1.
:- number_codes(N, "abc"), writeln(N).
:- ( 'abc' == abc -> writeln(same) ; writeln(different) ).
:- writeq('hello world'), nl.
:- print('it''s'), nl.
:- print([a, 'B', "c"]), nl.
:- print(f('A b', 'c')), nl.
:- print('[]'), nl, print([]), nl.
:- print('hello'), nl, print('Hello'), nl, print('_x'), nl, print(''), nl.
