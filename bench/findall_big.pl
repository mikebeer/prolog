% findall over a long list
upto(N, N, [N]) :- !.
upto(I, N, [I|T]) :- I1 is I + 1, upto(I1, N, T).
sq(X, Y) :- Y is X * X.
:- upto(1, 400, L), findall(Y, (member(X, L), sq(X, Y)), Ys), length(Ys, N), write(N), nl, last(Ys, Z), write(Z), nl.
:- findall(X-Y, (between(1, 20, X), between(1, 20, Y), X + Y =:= 21), Ps), length(Ps, N), write(N), nl.
