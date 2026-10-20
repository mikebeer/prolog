% Garbage collection: long deterministic loops run in bounded memory.  Without a
% collector every step leaves its frame, bindings and terms behind until the query
% ends.  (The C version also runs c/tests/c_only/gc_big.pl with millions of steps.)

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

:- count(2000), write(count_done), nl.
:- cnt(2000), write(cnt_done), nl.
:- sumsq(500, 0, S), write(S), nl.
:- steps(30, 0, T), write(T), nl.
:- fl(300, 0, R), write(R), nl.
:- lens(300, 0, S), write(S), nl.
:- assertz(counter(0)), bump(300), counter(C), write(C), nl.
:- fd(2000), write(fd_done), nl.
:- mklist(500, L), count(500), sum_list(L, S), length(L, N), write(N-S), nl.
:- X = f(_, Y), count(500), Y = g(_), print(X), nl.
