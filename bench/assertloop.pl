% assert and retract loop
count(0) :- !.
count(N) :- assertz(item(N)), N1 is N - 1, count(N1).
drain :- retract(item(_)), fail.
drain.
:- count(300), findall(X, item(X), L), length(L, N), write(N), nl.
:- drain, findall(X, item(X), L), length(L, N), write(N), nl.
:- assertz(cnt(0)).
bump(0) :- !.
bump(N) :- retract(cnt(C)), C1 is C + 1, assertz(cnt(C1)), N1 is N - 1, bump(N1).
:- bump(200), cnt(X), write(X), nl.
