% primes below 300 by trial division
prime(2).
prime(N) :- N > 2, N mod 2 =\= 0, nodiv(N, 3).
nodiv(N, D) :- D * D > N, !.
nodiv(N, D) :- N mod D =\= 0, D2 is D + 2, nodiv(N, D2).
upto(N, N, [N]) :- !.
upto(I, N, [I|T]) :- I1 is I + 1, upto(I1, N, T).
:- upto(2, 300, L), findall(P, (member(P, L), prime(P)), Ps), length(Ps, C), write(C), nl, last(Ps, X), write(X), nl.
