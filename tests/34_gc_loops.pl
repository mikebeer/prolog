% Long deterministic loops: before the garbage collector (and the
% iterative solver it needs) the CREXX version used ~100 KB per
% iteration, so these ~18,000 iterations would need ~1.8 GB.
count(0) :- !.
count(N) :- N > 0, N1 is N - 1, count(N1).
:- count(6000), writeln(count_done).
% base case last: the first-argument test sees that down(0) cannot
% match down(5), so no choice point is left
down(N) :- N > 0, N1 is N - 1, down(N1).
down(0).
:- down(4000), writeln(down_done).
sum(N, S) :- sum(N, 0, S).
sum(0, A, A) :- !.
sum(N, A, S) :- A1 is A + N, N1 is N - 1, sum(N1, A1, S).
:- sum(3000, S), writeln(S).
loop(I, N) :- ( I >= N -> true ; I1 is I + 1, loop(I1, N) ).
:- loop(0, 3000), writeln(ite_done).
mklist(0, L, L) :- !.
mklist(N, A, L) :- N1 is N - 1, mklist(N1, [N|A], L).
len([], N, N).
len([_|T], A, N) :- A1 is A + 1, len(T, A1, N).
:- mklist(2000, [], L), len(L, 0, N), writeln(N), last(L, X), writeln(X).
% variable names depend on the renaming counters, which GC must not touch
:- copy_term(f(A, B, A), C), print(C), nl.
:- count(100), length(L, 3), print(L), nl.
