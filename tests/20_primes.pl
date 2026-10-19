prime(2).
prime(N) :- N > 2, N mod 2 =\= 0, \+ has_factor(N, 3).
has_factor(N, F) :- F * F =< N, ( N mod F =:= 0 -> true ; F2 is F + 2, has_factor(N, F2) ).
:- findall(P, (between(1, 60, P), prime(P)), L), writeln(L).
:- findall(P, (between(100, 130, P), prime(P)), L), writeln(L).
sieve([], []).
sieve([P|Xs], [P|Ps]) :- remove_multiples(P, Xs, Ys), sieve(Ys, Ps).
remove_multiples(_, [], []).
remove_multiples(P, [X|Xs], Ys) :- ( X mod P =:= 0 -> Ys = Ys1 ; Ys = [X|Ys1] ), remove_multiples(P, Xs, Ys1).
range(L, H, []) :- L > H, !.
range(L, H, [L|T]) :- L1 is L + 1, range(L1, H, T).
:- range(2, 50, R), sieve(R, Ps), writeln(Ps).
:- range(2, 100, R), sieve(R, Ps), length(Ps, N), writeln(N).
nth_prime(N, P) :- nth_prime(N, 2, 0, P).
nth_prime(N, C, K, P) :- ( prime(C) -> K1 is K + 1 ; K1 = K ), ( K1 =:= N -> P = C ; C1 is C + 1, nth_prime(N, C1, K1, P) ).
:- nth_prime(25, P), writeln(P).
