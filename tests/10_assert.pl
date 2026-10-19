:- dynamic counter/1.
counter(0).
bump :- retract(counter(N)), N1 is N + 1, assert(counter(N1)).
:- bump, bump, bump, counter(X), writeln(X).
:- assert(color(red)), assert(color(green)), asserta(color(blue)), assertz(color(black)).
:- findall(C, color(C), L), writeln(L).
:- retract(color(green)), findall(C, color(C), L), writeln(L).
:- retractall(color(_)), findall(C, color(C), L), writeln(L).
:- color(red) -> writeln(still_red) ; writeln(no_red).
:- assert((double(X, Y) :- Y is X * 2)), double(4, Z), writeln(Z).
:- assert(sq(X, Y) :- Y is X * X), sq(5, Z), writeln(Z).
:- assertz(item(1)), assertz(item(2)), assertz(item(3)).
:- forall(retract(item(X)), (write(removed(X)), nl)).
:- findall(X, item(X), L), writeln(L).
:- assertz(fact(1)), assertz(fact(2)), assertz(fact(3)).
:- retract(fact(X)), X > 1, writeln(retracted(X)).
:- findall(X, fact(X), L), writeln(L).
:- retract((double(_, _) :- _)), writeln(rule_retracted).
:- catch_missing -> true ; true.
memo_fib(N, F) :- memo(N, F), !.
memo_fib(N, F) :- N < 2, !, F = N.
memo_fib(N, F) :- N1 is N - 1, N2 is N - 2, memo_fib(N1, F1), memo_fib(N2, F2), F is F1 + F2, assert(memo(N, F)).
:- memo_fib(30, F), writeln(F).
:- findall(N, memo(N, _), L), length(L, Len), writeln(Len).
:- assertz(p(X, X)), p(1, Y), writeln(Y).
:- assertz(q(f(A, B, A))), q(T), writeln(T).
:- listing(q).
:- retractall(nothing(_)), ( nothing(1) -> true ; writeln(none) ).
