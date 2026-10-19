% unification, comparison, term inspection
:- X = f(a, B), Y = f(A, b), X = Y, write(X-Y-A-B), nl.
:- ( f(a) = f(b) -> writeln(yes) ; writeln(no) ).
:- ( f(X, X) = f(1, 2) -> writeln(yes) ; writeln(no) ).
:- f(X, Y) = f(Y, 3), writeln(X-Y).
:- ( a \= b -> writeln(differ) ; writeln(same) ).
:- ( f(X) \= f(1) -> writeln(differ) ; writeln(unifiable) ).
:- ( X == X -> writeln(identical) ; writeln(not_identical) ).
:- ( X == Y -> writeln(identical) ; writeln(not_identical) ).
:- ( f(a) == f(a) -> writeln(identical) ; writeln(not_identical) ).
:- ( f(a) \== f(b) -> writeln(differ) ; writeln(same) ).
:- X = point(1, 2), functor(X, N, A), writeln(N/A).
:- functor(T, pair, 2), writeln(T).
:- functor(foo, N, A), writeln(N/A).
:- functor(7, N, A), writeln(N/A).
:- arg(2, f(a, b, c), X), writeln(X).
:- ( arg(4, f(a, b, c), X) -> writeln(X) ; writeln(no_arg) ).
:- f(a, b) =.. L, writeln(L).
:- T =.. [g, 1, 2, 3], writeln(T).
:- T =.. [atom], writeln(T).
:- copy_term(f(X, Y, X), C), writeln(C).
:- X = g(Y), Y = 1, writeln(X).
:- [H|T] = [1, 2, 3], writeln(H), writeln(T).
:- [A, B|R] = [x, y, z, w], writeln(A/B/R).
:- var(X), writeln(var_ok).
:- X = 1, nonvar(X), writeln(nonvar_ok).
:- atom(foo), atomic(foo), atomic(12), \+ atom(12), \+ atom(f(x)), writeln(types_ok).
:- number(3), number(2.5), integer(3), \+ integer(2.5), float(2.5), \+ float(3), writeln(numtypes_ok).
:- compound(f(x)), \+ compound(foo), callable(foo), callable(f(x)), \+ callable(3), writeln(compound_ok).
:- is_list([a, b]), \+ is_list([a|_]), \+ is_list(foo), writeln(list_ok).
:- ground(f(a, [1, 2])), \+ ground(f(_)), writeln(ground_ok).
:- X = 'abc', Y = abc, ( X == Y -> writeln(equal) ; writeln(quoted_differs) ).
