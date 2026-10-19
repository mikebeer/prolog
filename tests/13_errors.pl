% error messages
:- foo(1).
:- X is foo + 1.
:- X is Y + 1.
:- X is 1 / 0.
:- X is 5 mod 0.
:- X is 5 // 0.
:- X is 5 rem 0.
:- X is sqrt(-4).
:- X is cot(1).
:- X is foo(1, 2).
:- X is foo(1, 2, 3).
:- call(1).
:- call(X).
:- call(foo, 1, 2).
:- atom_length(X, 3).
:- atom_concat(X, b, ab).
:- number_codes(N, "12x").
:- number_chars(N, [a]).
:- 3 > a.
:- fail.
:- writeln(after_failures).
:- once(true).
:- catch(true, _, true).
:- throw(oops).
:- format("hello~n").
:- halt_not_called.
:- writeln(still_running).
