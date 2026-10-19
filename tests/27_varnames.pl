% the names of fresh variables show the renaming counters
p(X, Y) :- q(X, Z), r(Z, Y).
q(a, _).
q(_, b).
r(_, _).
:- p(A, B), writeln(A-B).
:- length(L, 2), writeln(L).
:- length(L, 3), L = [a|T], writeln(L/T).
:- copy_term(f(X, Y, Z, X), C), writeln(C).
:- functor(T, f, 3), writeln(T).
:- T =.. [g, A, B], writeln(T).
:- findall(X, member(X, [_, _]), L), writeln(L).
:- findall(Y, (member(X, [1, 2]), Y = X-_), L), writeln(L).
:- X = f(_, _A, _B, _A), writeln(X).
:- append(X, Y, [_, _]), writeln(X/Y), fail ; true.
:- between(1, 3, N), writeln(N), fail ; true.
:- assertz(w(f(A, B, A))), w(T), writeln(T).
:- listing(w).
:- retract(w(_)), assertz(w(g(_, _))), w(T), writeln(T).
:- copy_term(T, C), writeln(T/C).
:- X = Y, writeln(X/Y).
:- X = f(Y), Y = Z, writeln(X/Y/Z).
