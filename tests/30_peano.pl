nat(0).
nat(s(N)) :- nat(N).
plus_(0, Y, Y).
plus_(s(X), Y, s(Z)) :- plus_(X, Y, Z).
times(0, _, 0).
times(s(X), Y, Z) :- times(X, Y, Z0), plus_(Y, Z0, Z).
to_int(0, 0).
to_int(s(N), I) :- to_int(N, J), I is J + 1.
from_int(0, 0) :- !.
from_int(I, s(N)) :- I1 is I - 1, from_int(I1, N).
:- from_int(3, A), from_int(4, B), plus_(A, B, C), to_int(C, I), writeln(C), writeln(I).
:- from_int(3, A), from_int(4, B), times(A, B, C), to_int(C, I), writeln(I).
:- findall(X-Y, plus_(X, Y, s(s(0))), L), writeln(L).
:- plus_(X, s(0), s(s(s(0)))), writeln(X).
le(0, _).
le(s(X), s(Y)) :- le(X, Y).
:- ( le(s(0), s(s(0))) -> writeln(le) ; writeln(gt) ).
:- ( le(s(s(0)), s(0)) -> writeln(le) ; writeln(gt) ).
:- findall(N, (between(0, 6, I), from_int(I, N), le(N, s(s(0)))), L), length(L, Len), writeln(Len).
ack(0, N, R) :- !, R is N + 1.
ack(M, 0, R) :- !, M1 is M - 1, ack(M1, 1, R).
ack(M, N, R) :- M1 is M - 1, N1 is N - 1, ack(M, N1, R1), ack(M1, R1, R).
:- ack(2, 3, R), writeln(R).
