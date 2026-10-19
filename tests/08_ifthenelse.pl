sign(X, S) :- ( X > 0 -> S = positive ; X < 0 -> S = negative ; S = zero ).
:- sign(5, A), sign(-2, B), sign(0, C), writeln(A/B/C).
:- ( true -> writeln(then) ; writeln(else) ).
:- ( fail -> writeln(then) ; writeln(else) ).
:- ( member(X, [1, 2, 3]), X > 1 -> writeln(X) ; writeln(none) ).
:- ( fail -> writeln(then) ), writeln(unreachable) ; writeln(no_else_fails).
:- ( true -> ( fail -> writeln(a) ; writeln(b) ) ; writeln(c) ).
:- X = 5, ( X > 3 -> Y = big ; Y = small ), writeln(Y).
fizz(N) :- ( N mod 15 =:= 0 -> writeln(fizzbuzz) ; N mod 5 =:= 0 -> writeln(buzz) ; N mod 3 =:= 0 -> writeln(fizz) ; writeln(N) ).
:- forall(between(1, 15, N), fizz(N)).
:- ( true ; writeln(never) ), writeln(disjunction_first).
:- findall(X, ( X = 1 ; X = 2 ; X = 3 ), L), writeln(L).
:- ( 1 =:= 1 -> writeln(one) ), ( 2 =:= 2 -> writeln(two) ).
max3(A, B, C, M) :- ( A >= B, A >= C -> M = A ; B >= C -> M = B ; M = C ).
:- max3(1, 9, 5, M), writeln(M).
:- findall(X-Y, ( member(X, [1, 2]), ( X =:= 1 -> Y = a ; Y = b ) ), L), writeln(L).
