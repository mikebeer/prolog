:- foo(a, b) =.. L, writeln(L).
:- X =.. [foo, 1, 2], writeln(X).
:- X =.. [foo], writeln(X).
:- X =.. [3], writeln(X).
:- 3 =.. L, writeln(L).
:- foo =.. L, writeln(L).
:- [a, b] =.. L, writeln(L).
:- X =.. [+, 1, 2], writeln(X), Y is X, writeln(Y).
:- functor(f(a, b, c), N, A), writeln(N/A).
:- functor(T, g, 3), writeln(T).
:- functor([a], N, A), writeln(N/A).
:- functor(foo, N, A), writeln(N/A).
:- functor(T, foo, 0), writeln(T).
:- arg(1, g(x, y), A), arg(2, g(x, y), B), writeln(A/B).
:- ( arg(0, g(x), _) -> writeln(yes) ; writeln(no) ).
:- ( arg(3, g(x), _) -> writeln(yes) ; writeln(no) ).
:- ( arg(1, foo, _) -> writeln(yes) ; writeln(no) ).
:- T = f(a, B), arg(2, T, z), writeln(T).
:- foo(X, Y) =.. [F|Args], writeln(F), length(Args, N), writeln(N).
:- G =.. [writeln, hello], call(G).
:- G =.. [append, [1], [2], L], call(G), writeln(L).
:- copy_term(f(X, g(Y), X), C), writeln(C).
:- copy_term(a, C), writeln(C).
:- copy_term(f(1, 2), C), writeln(C).
:- X = f(Y), copy_term(X, Z), Z = f(1), writeln(X/Z).
:- call(writeln, x).
:- call(append([1]), [2], L), writeln(L).
:- G = format_not_there, call(G).
:- maplist(call, [true, true]), writeln(ok).
