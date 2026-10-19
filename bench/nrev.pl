% naive reverse of a 30 element list, repeated
app([], L, L).
app([H|T], L, [H|R]) :- app(T, L, R).
nrev([], []).
nrev([H|T], R) :- nrev(T, RT), app(RT, [H], R).
range(N, N, [N]) :- !.
range(I, N, [I|T]) :- I1 is I + 1, range(I1, N, T).
loop(0) :- !.
loop(N) :- range(1, 30, L), nrev(L, _), N1 is N - 1, loop(N1).
:- loop(20), range(1, 30, L), nrev(L, R), write(R), nl.
