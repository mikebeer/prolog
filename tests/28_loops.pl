count(N, N) :- !.
count(I, N) :- I1 is I + 1, count(I1, N).
:- count(0, 3000), writeln(counted_3000).
sum_to(0, 0) :- !.
sum_to(N, S) :- N1 is N - 1, sum_to(N1, S1), S is S1 + N.
:- sum_to(1000, S), writeln(S).
:- findall(X, between(1, 300, X), L), length(L, N), sum_list(L, S), writeln(N/S).
:- between(1, 5, X), write(X), write(' '), fail ; nl.
:- forall(between(1, 5, X), (Y is X * X, write(Y), write(' '))), nl.
len([], 0).
len([_|T], N) :- len(T, M), N is M + 1.
mk(0, []) :- !.
mk(N, [N|T]) :- N1 is N - 1, mk(N1, T).
:- mk(500, L), len(L, N), writeln(N), last(L, X), writeln(X).
:- mk(200, L), reverse(L, R), nth0(0, R, F), writeln(F).
even(0).
even(N) :- N > 0, M is N - 1, odd(M).
odd(N) :- N > 0, M is N - 1, even(M).
:- ( even(100) -> writeln(even) ; writeln(odd) ).
:- ( even(7) -> writeln(even) ; writeln(odd) ).
collatz(1, 0) :- !.
collatz(N, S) :- ( N mod 2 =:= 0 -> M is N // 2 ; M is 3 * N + 1 ), collatz(M, S0), S is S0 + 1.
:- collatz(27, S), writeln(S).
pow2(0, 1) :- !.
pow2(N, P) :- N1 is N - 1, pow2(N1, P1), P is 2 * P1.
:- pow2(40, P), writeln(P).
:- pow2(60, P), writeln(P).
repeat_write(0, _) :- !.
repeat_write(N, X) :- write(X), N1 is N - 1, repeat_write(N1, X).
:- repeat_write(10, '*'), nl.
