% Last-call elimination must not change what happens after a failure:
% cut signals, and the renaming steps of clauses skipped by the
% first-argument test (they show up in the printed variable names).
r(N) :- N > 0, N1 is N - 1, r(N1).
r(0) :- fail.
r(a(X)).
:- ( r(5) -> true ; true ), copy_term(f(Q), C), print(C), nl.
t(N) :- N > 0, !, N1 is N - 1, t(N1).
t(0) :- !, fail.
t(b(Y, Z)).
:- ( t(3) -> writeln(yes) ; writeln(no) ), copy_term(g(Z), C), print(C), nl.
t2(N) :- N > 0, N1 is N - 1, t2(N1), !.
t2(0) :- fail.
t2(c(P, Q, R)).
:- ( t2(4) -> writeln(yes) ; writeln(no) ), copy_term(h(Z), C), print(C), nl.
u(N, L) :- N > 0, N1 is N - 1, ( u(N1, L) ; L = alt(N) ).
u(0, zero).
u(k(A, B), kk).
:- findall(L, u(3, L), Ls), print(Ls), nl, copy_term(i(Z), C), print(C), nl.
v(N) :- N > 0, ( N > 2, ! , fail ; true ), N1 is N - 1, v(N1).
v(0).
v(z(A)).
:- ( v(5) -> writeln(v5yes) ; writeln(v5no) ), ( v(2) -> writeln(v2yes) ; writeln(v2no) ), copy_term(j(Z), C), print(C), nl.
w(N) :- N > 0, \+ ( N = 3, ! ), N1 is N - 1, w(N1).
w(0) :- writeln(w_end).
w(q(A, B, C)).
:- ( w(5) -> true ; writeln(w_failed) ), copy_term(k(Z), C), print(C), nl.
x(N, S) :- N > 0, N1 is N - 1, x(N1, S1), S is S1 + N.
x(0, 0).
x(m(A), 0).
:- x(50, S), writeln(S), copy_term(l(Z), C), print(C), nl.
y(N) :- N > 0, ( N =:= 2 -> fail ; true ), N1 is N - 1, y(N1).
y(0).
y(n(E)).
:- ( y(4) -> writeln(y4) ; writeln(noy4) ), copy_term(m(Z), C), print(C), nl.
z(N) :- N > 0, !, N1 is N - 1, z(N1), fail.
z(0).
z(o(A, B)).
:- ( z(3) -> writeln(z3) ; writeln(noz3) ), copy_term(n(Z), C), print(C), nl.
