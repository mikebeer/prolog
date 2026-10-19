p('hello world').
p('Hello').
p(hello).
p('it''s').
p([]).
p('[]').
p('').
p('a.b').
p(+).
p('+').
p('/*').
p(f(' x ')).
:- forall(p(X), (print(X), nl)).
:- forall(p(X), (write(X), nl)).
:- ( p(hello) -> writeln(yes) ; writeln(no) ).
:- ( p('hello') -> writeln(yes) ; writeln(no) ).
:- findall(X, p(X), L), print(L), nl.
:- p('Hello'), writeln(found).
:- X = 'hello', Y = hello, ( X = Y -> writeln(unify) ; writeln(no_unify) ), ( X == Y -> writeln(identical) ; writeln(not_identical) ).
:- atom_codes(X, "hello"), ( X = hello -> writeln(unify) ; writeln(no_unify) ), ( X == hello -> writeln(identical) ; writeln(not_identical) ).
:- listing(p).
