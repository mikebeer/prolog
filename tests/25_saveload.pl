fact(1, one). fact(2, two).
rule(X) :- fact(X, _), X > 1.
big(X, Y) :- ( X > 0 -> Y = pos ; X < 0 -> Y = neg ; Y = zero ), \+ Y = foo, !.
:- save('t25_saved.dat').
:- assertz(extra(1)), assertz((extra(X) :- X > 5, foo(X))).
:- save('t25_saved.dat').
:- retractall(fact(_, _)), retractall(rule(_)), retractall(big(_, _)).
:- retractall(extra(_)).
:- consult('t25_saved.dat').
:- fact(2, X), writeln(X).
:- ( rule(2) -> writeln(rule_ok) ; writeln(rule_failed) ).
:- big(-1, Y), writeln(Y).
:- listing(big).
:- listing(extra).
:- save_all('t25_all.dat').
:- consult('t25_saved.dat').
:- findall(X, fact(X, _), L), writeln(L).
