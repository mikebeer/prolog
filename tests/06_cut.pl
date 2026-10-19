max(X, Y, X) :- X >= Y, !.
max(_, Y, Y).
:- max(3, 5, M), writeln(M).
:- max(7, 5, M), writeln(M).
first([X|_], X) :- !.
:- first([a, b, c], F), writeln(F).
a(1). a(2). a(3).
b(X) :- a(X), X > 1, !.
:- b(X), writeln(X).
:- findall(X, b(X), L), writeln(L).
c(X) :- a(X).
c(4).
:- findall(X, c(X), L), writeln(L).
d(X) :- a(X), !.
d(4).
:- findall(X, d(X), L), writeln(L).
e(X, Y) :- a(X), a(Y), Y > X, !.
:- e(X, Y), writeln(X-Y).
f(X) :- ( a(X), X > 1 -> true ; X = none ).
:- f(X), writeln(X).
len([], 0).
len([_|T], N) :- len(T, M), N is M + 1.
classify(N, neg) :- N < 0, !.
classify(0, zero) :- !.
classify(_, pos).
:- classify(-3, A), classify(0, B), classify(8, C), writeln(A/B/C).
g(X) :- a(X), writeln(trying(X)), X >= 2, !, writeln(committed(X)).
g(_) :- writeln(second_clause).
:- g(X), writeln(result(X)).
h(1) :- !, fail.
h(_).
:- ( h(1) -> writeln(h_true) ; writeln(h_false) ).
:- ( h(2) -> writeln(h_true) ; writeln(h_false) ).
t(X) :- a(X), \+ X = 2.
:- findall(X, t(X), L), writeln(L).
