% operator printing (the operator table is fixed; op/3 is not supported)
:- write(1 + 2 * 3), nl.
:- write((1 + 2) * 3), nl.
:- write(1 - 2 - 3), nl.
:- write(1 - (2 - 3)), nl.
:- write(2 ^ 3 ^ 4), nl.
:- write((2 ^ 3) ^ 4), nl.
:- write(a = b), nl.
:- write((a :- b, c)), nl.
:- write((a , b)), nl.
:- write((a ; b)), nl.
:- write((a -> b ; c)), nl.
:- write(\+ a), nl.
:- write(\+ (a, b)), nl.
:- write(- a), nl.
:- write(- (1)), nl.
:- write(-(-(1))), nl.
:- write(- 1), nl.
:- write(1 - -1), nl.
:- write(a - (-1)), nl.
:- write(f(a + b, c * d)), nl.
:- write(f((a , b))), nl.
:- write([a + b, c]), nl.
:- write(X is 1 + 2), nl.
:- write(1 =< 2), nl.
:- write(a =.. b), nl.
:- write(a mod b), nl.
:- write(a rem b), nl.
:- write(1 / 2 / 3), nl.
:- write(1 / (2 / 3)), nl.
:- write(a * (b + c)), nl.
:- write((a + b) * c), nl.
:- write(2 ** 3), nl.
:- write(\ a), nl.
:- write(a /\ b), nl.
:- write(a \/ b), nl.
:- write(a << b), nl.
:- print(a + 'B c'), nl.
:- X = (a :- b), write(X), nl.
:- op(700, xfx, ===), writeln(op_ok).
:- X = 'hello_world'(1), write(X), nl.
:- write('$VAR'(1)), nl.
:- write({a}), nl.
