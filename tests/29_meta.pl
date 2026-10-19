add(X, Y, Z) :- Z is X + Y.
inc(X, Y) :- Y is X + 1.
sq(X, Y) :- Y is X * X.
:- call(add, 1, 2, Z), writeln(Z).
:- call(add(10), 5, Z), writeln(Z).
:- maplist(inc, [1, 2, 3], L), writeln(L).
:- maplist(sq, [1, 2, 3, 4], L), writeln(L).
:- maplist(add, [1, 2], [10, 20], L), writeln(L).
:- G = (member(X, [1, 2, 3]), X > 1), call(G), writeln(X).
:- G = writeln(hi), call(G).
:- call((writeln(a), writeln(b))).
:- findall(X, call(member, X, [p, q]), L), writeln(L).
twice(G) :- call(G), call(G).
:- twice(write(x)), nl.
compose(F, G, X, Z) :- call(G, X, Y), call(F, Y, Z).
:- compose(inc, sq, 4, R), writeln(R).
:- foldl_(add, [1, 2, 3, 4], 0, S), writeln(S).
foldl_(_, [], A, A).
foldl_(P, [X|Xs], A0, A) :- call(P, X, A0, A1), foldl_(P, Xs, A1, A).
:- include([X]>>(X > 1), [1, 2, 3], L), writeln(L).
:- forall(member(X, [1, 2]), (write(X), nl)).
:- ( call(fail) -> writeln(yes) ; writeln(no) ).
:- \+ call(fail), writeln(negated).
:- G = fail, ( G -> writeln(yes) ; writeln(no) ).
