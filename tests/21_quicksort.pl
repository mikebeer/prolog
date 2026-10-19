qsort([], []).
qsort([P|Xs], S) :- partition_(Xs, P, L, G), qsort(L, SL), qsort(G, SG), append(SL, [P|SG], S).
partition_([], _, [], []).
partition_([X|Xs], P, [X|L], G) :- X =< P, !, partition_(Xs, P, L, G).
partition_([X|Xs], P, L, [X|G]) :- partition_(Xs, P, L, G).
:- qsort([3, 1, 4, 1, 5, 9, 2, 6, 5, 3, 5], S), writeln(S).
:- qsort([c, a, b], S), writeln(S).
:- qsort([], S), writeln(S).
msort2([], []) :- !.
msort2([X], [X]) :- !.
msort2(L, S) :- split(L, A, B), msort2(A, SA), msort2(B, SB), merge_(SA, SB, S).
split([], [], []).
split([X], [X], []).
split([X, Y|T], [X|A], [Y|B]) :- split(T, A, B).
merge_([], L, L) :- !.
merge_(L, [], L) :- !.
merge_([X|Xs], [Y|Ys], [X|Zs]) :- X =< Y, !, merge_(Xs, [Y|Ys], Zs).
merge_(Xs, [Y|Ys], [Y|Zs]) :- merge_(Xs, Ys, Zs).
:- msort2([8, 3, 7, 1, 9, 2, 6], S), writeln(S).
isort([], []).
isort([X|Xs], S) :- isort(Xs, S0), insert_(X, S0, S).
insert_(X, [], [X]).
insert_(X, [Y|Ys], [X, Y|Ys]) :- X =< Y, !.
insert_(X, [Y|Ys], [Y|Zs]) :- insert_(X, Ys, Zs).
:- isort([5, 2, 4, 6, 1, 3], S), writeln(S).
