% C version only: long deterministic loops that need the garbage collector.
% Without it (PROLOG_GC=off, or the version before the collector) the first
% directive alone grows to about 550 MB and the whole file runs for half an hour;
% with it the loops run in a few MB (the peak, about 60 MB, is the live list of
% the mklist directive).  See c/README.md, "Memory and garbage collection".
% tail recursion, the recursive clause last
count(0).
count(N) :- N > 0, N1 is N - 1, count(N1).

% the recursive clause first, committed by a cut
cnt(N) :- N > 0, !, N1 is N - 1, cnt(N1).
cnt(0).

% a term built and dropped at every step
sumsq(0, S, S) :- !.
sumsq(N, A, S) :- L = [N, N, N], sum_list(L, T), A1 is A + T * T, N1 is N - 1, sumsq(N1, A1, S).

% if-then-else in the loop body
collatz(1, S, S) :- !.
collatz(N, A, S) :-
  ( N mod 2 =:= 0 -> M is N // 2 ; M is 3 * N + 1 ), A1 is A + 1, collatz(M, A1, S).
steps(0, T, T) :- !.
steps(N, A, T) :- collatz(N, 0, S), A1 is A + S, N1 is N - 1, steps(N1, A1, T).

% findall, atoms and lists inside the loop
fl(0, A, A) :- !.
fl(N, A, R) :-
  findall(X-N, member(X, [a,b,c]), L), length(L, K),
  atom_codes(At, [120, 121]), atom_length(At, AL), msort([c,b,a], [F|_]),
  ( F == a -> A1 is A + K + AL ; A1 = A ), N1 is N - 1, fl(N1, A1, R).

% first-argument selection on lists ([] against [_|_]) leaves no choice point
len([], N, N).
len([_|T], A, N) :- A1 is A + 1, len(T, A1, N).
lens(0, S, S) :- !.
lens(N, A, S) :- len([x, y, z], 0, K), A1 is A + K, N1 is N - 1, lens(N1, A1, S).

% a counter kept in the database: every step retracts a clause
bump(0) :- !.
bump(N) :- retract(counter(C)), C1 is C + 1, assertz(counter(C1)), N1 is N - 1, bump(N1).

% a failure-driven loop
fd(N) :- between(1, N, I), I < 0.
fd(_).

% live data that has to survive the collections
mklist(0, []) :- !.
mklist(N, [N|T]) :- N1 is N - 1, mklist(N1, T).

% naive reverse in a loop: app/3 and nrev/2 select their clause on the first argument
app([], L, L).
app([H|T], L, [H|R]) :- app(T, L, R).
nrev([], []).
nrev([H|T], R) :- nrev(T, RT), app(RT, [H], R).
range(N, N, [N]) :- !.
range(I, N, [I|T]) :- I1 is I + 1, range(I1, N, T).
nloop(0) :- !.
nloop(N) :- range(1, 30, L), nrev(L, _), N1 is N - 1, nloop(N1).

% copy_term, =.. and assert of fresh clauses, then retract them all
churn(0) :- !.
churn(N) :- T = t(N, _, f(_)), copy_term(T, C), C =.. [_|As], length(As, 3),
  assertz(tmp(N, C)), ( N mod 100 =:= 0 -> retractall(tmp(_, _)) ; true ),
  N1 is N - 1, churn(N1).

:- count(1000000), write(count_done), nl.
:- cnt(500000), write(cnt_done), nl.
:- sumsq(100000, 0, S), write(S), nl.
:- steps(1000, 0, T), write(T), nl.
:- fl(30000, 0, R), write(R), nl.
:- lens(100000, 0, S), write(S), nl.
:- assertz(counter(0)), bump(50000), counter(C), write(C), nl.
:- fd(200000), write(fd_done), nl.
:- nloop(5000), write(nrev_done), nl.
:- churn(50000), findall(x, tmp(_, _), L), length(L, N), write(N), nl.
:- mklist(50000, L), count(300000), sum_list(L, S), length(L, N), write(N-S), nl.
:- X = f(_, Y), count(100000), Y = g(_), print(X), nl.
:- garbage_collect.
