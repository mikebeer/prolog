% garbage collection with choice points, cut, findall and the top level
count(0) :- !.
count(N) :- N > 0, N1 is N - 1, count(N1).
:- between(1, 2000, X), Y is X * X, Y < 0 ; writeln(fail_loop_done).
sq(N, S) :- count(20), S is N * N.
:- findall(S, (between(1, 200, N), sq(N, S)), L), sum_list(L, T), writeln(T).
first(X) :- member(X, [a, b, c]), count(1000), X \== a, !.
:- first(X), writeln(X).
alt(X) :- ( count(1000), X = left ; count(1000), X = right ).
:- findall(X, alt(X), L), writeln(L).
neg(N) :- \+ ( count(N), fail ), writeln(neg_ok).
:- neg(2000).
big(N, X, V) :- count(N), member(X, [a, b, c]), count(N), copy_term(g(V), V).
