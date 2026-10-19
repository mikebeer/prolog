% cut inside a disjunction or condition
a(1). a(2). a(3).
t1(X) :- ( a(X), X > 1, ! ; X = none ).
:- findall(X, t1(X), L), writeln(L).
t2(X) :- a(X), ( X > 1 -> ! ; true ).
:- findall(X, t2(X), L), writeln(L).
t3(X) :- ( ! , fail ; X = reached ).
:- ( t3(X) -> writeln(X) ; writeln(failed) ).
t4(X) :- a(X), \+ ( X = 2, ! ).
:- findall(X, t4(X), L), writeln(L).
t5(X) :- call(( a(X), ! )).
:- findall(X, t5(X), L), writeln(L).
t6(X) :- findall(Y, ( a(Y), ! ), X).
:- t6(X), writeln(X).
t7 :- a(X), X > 1, !, writeln(t7(X)).
:- t7.
:- t7, t7.
t8(X) :- ( a(X) ; X = 4 ), X > 2.
:- findall(X, t8(X), L), writeln(L).
t9 :- !, writeln(cut_in_directive).
:- t9.
:- !, writeln(bare_cut).
:- a(X), !, writeln(X).
t10(X) :- a(X), ( true ; true ).
:- findall(X, t10(X), L), writeln(L).
