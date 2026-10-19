% assert/retract: retracted clauses are freed once no call can still be
% iterating over them; a call that is iterating keeps them alive
count(0) :- !.
count(N) :- N1 is N - 1, count(N1).
:- assertz(cnt(0)).
bump(0) :- !.
bump(N) :- retract(cnt(C)), C1 is C + 1, assertz(cnt(C1)), N1 is N - 1, bump(N1).
:- bump(3000), cnt(X), writeln(X).
item(1). item(2). item(3).
:- findall(X, (item(X), retract(item(X))), L), writeln(L).
:- findall(X, item(X), L), writeln(L).
p(1). p(2). p(3). p(4).
:- p(X), retractall(p(_)), assertz(q(X)), count(200), X >= 3, !, findall(Y, q(Y), L), writeln(L).
:- findall(X, p(X), L), writeln(L).
grow(0) :- !.
grow(N) :- assertz(fact(N, [N|_])), N1 is N - 1, grow(N1).
:- grow(500), findall(N, fact(N, _), L), length(L, Len), writeln(Len).
:- retract(fact(250, T)), print(T), nl.
:- retractall(fact(_, _)), findall(N, fact(N, _), L), writeln(L).
