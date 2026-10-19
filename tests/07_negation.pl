p(1). p(2). p(3).
q(2).
:- \+ q(1), writeln(not_q1).
:- ( \+ q(2) -> writeln(yes) ; writeln(no) ).
:- findall(X, (p(X), \+ q(X)), L), writeln(L).
:- ( not(p(5)) -> writeln(no_p5) ; writeln(has_p5) ).
:- \+ \+ p(1), writeln(double_negation).
:- X = 1, \+ X = 2, writeln(ok).
:- \+ fail, writeln(fail_negated).
:- ( \+ true -> writeln(a) ; writeln(b) ).
r(X) :- p(X), \+ q(X).
:- findall(X, r(X), L), writeln(L).
:- \+ ( p(X), X > 5 ), writeln(none_greater_than_5).
:- X = f(Y), \+ Y = 1, writeln(X).
disjoint(A, B) :- \+ ( member(X, A), member(X, B) ).
:- ( disjoint([1, 2], [3, 4]) -> writeln(disjoint) ; writeln(overlap) ).
:- ( disjoint([1, 2], [2, 4]) -> writeln(disjoint) ; writeln(overlap) ).
