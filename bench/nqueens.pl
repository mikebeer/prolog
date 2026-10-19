% six queens, all solutions counted
queens(N, Qs) :- range(1, N, Ns), perm(Ns, Qs), safe(Qs).
range(N, N, [N]) :- !.
range(I, N, [I|T]) :- I1 is I + 1, range(I1, N, T).
sel(X, [X|T], T).
sel(X, [H|T], [H|R]) :- sel(X, T, R).
perm([], []).
perm(L, [H|T]) :- sel(H, L, R), perm(R, T).
safe([]).
safe([Q|Qs]) :- noattack(Q, Qs, 1), safe(Qs).
noattack(_, [], _).
noattack(Q, [Q1|Qs], D) :- Q =\= Q1 + D, Q =\= Q1 - D, D1 is D + 1, noattack(Q, Qs, D1).
:- findall(Q, queens(6, Q), L), length(L, N), write(N), nl, L = [F|_], write(F), nl.
