% numbers are 15 significant digits (see the port notes)
fact(0, 1) :- !.
fact(N, F) :- N1 is N - 1, fact(N1, F1), F is N * F1.
:- fact(10, F), writeln(F).
:- fact(15, F), writeln(F).
:- fact(17, F), writeln(F).
:- fact(20, F), writeln(F).
:- fact(25, F), writeln(F).
:- fact(30, F), writeln(F).
:- fact(50, F), writeln(F).
fib(0, 0, 1) :- !.
fib(N, A, B) :- N1 is N - 1, fib(N1, A0, B0), A is B0, B is A0 + B0.
:- fib(50, A, _), writeln(A).
:- fib(80, A, _), writeln(A).
:- fib(100, A, _), writeln(A).
:- X is 2 ** 62, writeln(X).
:- X is 2 ** 100, writeln(X).
:- X is 3 ** 40, writeln(X).
:- X is 7 ** 25, writeln(X).
:- X is 10 ** 17, writeln(X).
:- X is 10 ** 18, writeln(X).
:- X is 123456789012345678 * 1, writeln(X).
:- X is 99999999999999999 + 0, writeln(X).
:- X is 1234567890123456 * 10, writeln(X).
:- X is 99999999999 * 99999999999, writeln(X).
:- X is 1000000 * 1000000, writeln(X).
:- X is 1000000 * 1000000 * 1000000, writeln(X).
:- X is 1e20 + 1, writeln(X).
:- X is 12345678901234567890, writeln(X).
:- X is 12345678901234567890 + 0, writeln(X).
:- X is 9223372036854775807 + 1, writeln(X).
:- X is 100000000000000000000 // 7, writeln(X).
:- X is 10 ** 20 mod 7, writeln(X).
:- ( 12345678901234567890 =:= 12345678901234567891 -> writeln(equal_at_15_digits) ; writeln(different) ).
:- X is 2 ** 0.5, writeln(X).
:- X is truncate(1e20), writeln(X).
:- X is 1.5e300 * 1.5e300, writeln(X).
:- X is 10 ** 308, writeln(X).
:- X is 10 ** 309, writeln(X).
:- X is 1/3*3, writeln(X).
:- X is 1/3, Y is X*3, writeln(Y).
