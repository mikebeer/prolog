:- X is 3.14 * 2, writeln(X).
:- X is 1 / 3, writeln(X).
:- X is 2 / 3, writeln(X).
:- X is 22 / 7, writeln(X).
:- X is 7 / 2, writeln(X).
:- X is 0.1 + 0.2, writeln(X).
:- X is 1 - 0.9, writeln(X).
:- X is 1.5 + 1.5, writeln(X).
:- X is 2.50 * 1, writeln(X).
:- X is sqrt(16), writeln(X).
:- X is sqrt(2), writeln(X).
:- X is sqrt(10), writeln(X).
:- X is pi, writeln(X).
:- X is e, writeln(X).
:- X is pi * 2, writeln(X).
:- X is 1.0e10, writeln(X).
:- X is 1.0e10 + 0, writeln(X).
:- X is 0.000001 * 1, writeln(X).
:- X is 0.0000001 * 1, writeln(X).
:- X is 1.5e-10 * 1, writeln(X).
:- X is 1.5e17 * 1, writeln(X).
:- X is 1.5e18 * 1, writeln(X).
:- X is 1e100 * 1, writeln(X).
:- X is 3.0, writeln(X).
:- X is 3.0 + 1, writeln(X).
:- X is float(3), writeln(X).
:- X is truncate(2.9), Y is truncate(-2.9), writeln(X/Y).
:- X is round(2.5), Y is round(-2.5), Z is round(0.5), writeln(X/Y/Z).
:- X is floor(2.5), Y is ceiling(2.5), writeln(X/Y).
:- X is abs(-2.5), writeln(X).
:- X is -2.5, Y is - X, writeln(Y).
:- X is max(3, 4.0), Y is min(3, 3.0), writeln(X/Y).
:- X is 2 ** -2, writeln(X).
:- X is 2.0 ** 3, writeln(X).
:- X is 10 ** -3, writeln(X).
:- X is 5.5 mod 2, writeln(X).
:- ( 0.5 < 1 -> writeln(lt) ; writeln(ge) ).
:- ( 1.0 =:= 1 -> writeln(eq) ; writeln(ne) ).
:- ( 1.0 = 1 -> writeln(unify) ; writeln(no_unify) ).
:- ( float(1.0) -> writeln(float) ; writeln(not_float) ).
:- ( integer(1.0) -> writeln(integer) ; writeln(not_integer) ).
:- X is 100 / 7, writeln(X).
:- X is 123456.789e3, writeln(X).
:- X is 1.0e-5 + 0, writeln(X).
:- X is -0.5 // 1, writeln(X).
:- X is truncate(-0.5), writeln(X).
