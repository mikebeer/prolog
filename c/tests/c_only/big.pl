% C version only (the CREXX version is far too slow for these sizes):
% msort/sort of a long list and a large assert-based table (first-argument index)
gen(I, N, _, []) :- I > N, !.
gen(I, N, X, [Y|T]) :- Y is X * 16807 mod 2147483647, I1 is I + 1, gen(I1, N, Y, T).
:- gen(1, 50000, 42, L), msort(L, S), M is 50000 // 2, nth0(M, S, Mid), S = [F|_], last(S, La),
   R is (F + Mid + La) mod 1000003, write(R), nl.
:- gen(1, 200000, 42, L), msort(L, S), length(S, N), nth0(100000, S, Mid), last(S, La),
   write(N-Mid-La), nl.
:- findall(X, (between(1, 3000, I), X is I mod 7), L), sort(L, S), msort(L, M), length(M, N),
   write(S-N), nl.
:- findall(f(X, Y), (between(1, 100, I), X is I mod 5, Y is I mod 3), L), sort(L, S), length(S, N),
   write(N), nl, S = [A, B|_], write(A-B), nl.
ins(I, N) :- I > N, !.
ins(I, N) :- K is I * 7919 mod 1000003, assertz(kv(K, I)), I1 is I + 1, ins(I1, N).
look(I, N, S, S) :- I > N, !.
look(I, N, A, S) :- K is I * 7919 mod 1000003, kv(K, V), !, A1 is A + V, I1 is I + 1,
   look(I1, N, A1, S).
:- ins(1, 100000), look(1, 100000, 0, S), R is S mod 1000003, write(R), nl.
:- assertz(kv(special, 1)), assertz(kv(X, 2)), assertz(kv(special, 3)),
   findall(V, kv(special, V), L), write(L), nl, findall(V, kv(nothere, V), L2), write(L2), nl,
   retract(kv(special, 1)), findall(V, kv(special, V), L3), write(L3), nl.
