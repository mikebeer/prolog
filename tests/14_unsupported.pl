% features the CREXX version does not have: they report unknown procedures
:- catch(throw(my_error), E, (write(caught(E)), nl)).
:- catch(X is 1/0, error(E, _), (write(E), nl)).
:- bagof(X, member(X, [a, b]), L), writeln(L).
:- setof(X, member(X, [b, a]), L), writeln(L).
:- format("~w and ~a~n", [x, y]).
:- succ(3, X), writeln(X).
:- plus(1, 2, X), writeln(X).
:- sub_atom(hello, 1, 3, _, S), writeln(S).
:- nb_setval(k, 1).
:- string_concat("a", "b", S).
:- number_vars(f(X), 0, _).
:- tab(2), writeln(x).
greeting --> [hello], name.
name --> [world].
:- listing(greeting).
:- phrase(greeting, [hello, world]).
:- length(L, 2), L = [a|_], writeln(L).
:- last([1, 2, 3], X), sumlist([1], _) -> true ; true.
:- msort([b, a], L), writeln(L).
:- predsort(compare, [b, a], L), writeln(L).
:- nth_clause(a, 1, R).
:- apply(writeln, [x]).
:- ignore(fail), writeln(ignored).
:- once(member(X, [1, 2])), writeln(X).
:- between(1, inf, X), X > 3, !, writeln(X).
