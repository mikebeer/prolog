:- write(hello), nl.
:- write('hello world'), nl.
:- print('hello world'), nl.
:- writeln([1, 2, 3]).
:- writeln([a|b]).
:- writeln('A'), print('A'), nl.
:- write("a string"), nl.
:- write(f(X, Y, X)), nl.
:- write(f(_, _)), nl.
:- print(f("code list")), nl.
:- tab(3), write(x), tab(1 + 1), write(y), nl.
:- write(a), write(' '), write(b), nl.
:- write([]), nl, write('[]'), nl.
:- write({}), nl.
:- write('don''t'), nl, print('don''t'), nl.
:- write(f(a, 'B', "c", 1.5, [x])), nl.
:- print(f(a, 'B', "c", 1.5, [x])), nl.
:- writeln(- 5), writeln(1 - 2), writeln(1-2), writeln(f(1 - 2)).
:- writeln(1.0), writeln(1.5e10), writeln(0.1), writeln(100000000000000000000).
:- X = point(1, 2), print(X), nl, write(X), nl, writeln(X).
:- write(f(a;b)), nl.
:- write((a:-b;c)), nl.
:- write([(a,b)]), nl.
:- write(- - a), nl.
:- write(1 + 2 + 3), nl, write(1 + (2 + 3)), nl.
:- write(a:b:c), nl.
:- write(f(:-, -, +)), nl.
:- write('hello\\world'), nl.
:- print(abc123), nl, print(aBC), nl, print('1abc'), nl, print('a-b'), nl, print(+), nl, print(++), nl, print('+a'), nl.
:- print(;), nl, print('!'), nl, print(','), nl, print('|'), nl.
