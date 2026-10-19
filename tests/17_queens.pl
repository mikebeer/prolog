queens(N, Qs) :- numlist_(1, N, Ns), permutation(Ns, Qs), safe(Qs).
numlist_(N, N, [N]) :- !.
numlist_(I, N, [I|T]) :- I < N, I1 is I + 1, numlist_(I1, N, T).
safe([]).
safe([Q|Qs]) :- no_attack(Q, Qs, 1), safe(Qs).
no_attack(_, [], _).
no_attack(Q, [Q1|Qs], D) :- Q =\= Q1 + D, Q =\= Q1 - D, D1 is D + 1, no_attack(Q, Qs, D1).
:- queens(4, Qs), writeln(Qs).
:- findall(Qs, queens(4, Qs), L), writeln(L).
:- findall(Qs, queens(5, Qs), L), length(L, N), writeln(N).
:- queens(6, Qs), writeln(Qs).
% incremental version
place(0, _, []) :- !.
place(K, N, [Q|Qs]) :- K1 is K - 1, place(K1, N, Qs), between(1, N, Q), ok(Q, Qs, 1).
ok(_, [], _).
ok(Q, [Q1|Qs], D) :- Q =\= Q1, Q =\= Q1 + D, Q =\= Q1 - D, D1 is D + 1, ok(Q, Qs, D1).
:- place(6, 6, Qs), writeln(Qs).
:- findall(Q, place(6, 6, Q), L), length(L, N), writeln(solutions(N)).
