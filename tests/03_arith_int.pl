:- X is 2 + 3 * 4, writeln(X).
:- X is (2 + 3) * 4, writeln(X).
:- X is 10 - 4 - 3, writeln(X).
:- X is 7 // 2, Y is -7 // 2, writeln(X/Y).
:- X is 7 mod 3, Y is -7 mod 3, Z is 7 mod -3, writeln(X/Y/Z).
:- X is 7 rem 3, Y is -7 rem 3, writeln(X/Y).
:- X is 2 ** 10, writeln(X).
:- X is abs(-5), Y is sign(-5), Z is sign(0), writeln(X/Y/Z).
:- X is max(3, 7), Y is min(3, 7), writeln(X/Y).
:- X is - (3), Y is - (-3), writeln(X/Y).
:- ( 3 < 4 -> writeln(lt) ; writeln(ge) ).
:- ( 4 =< 4 -> writeln(le) ; writeln(gt) ).
:- ( 5 > 4, 5 >= 5 -> writeln(gt_ge) ; writeln(no) ).
:- ( 2 + 2 =:= 4 -> writeln(equal) ; writeln(different) ).
:- ( 2 + 2 =\= 5 -> writeln(not_equal) ; writeln(equal) ).
:- X = 3, Y is X * X + 1, writeln(Y).
:- X is truncate(3.7), Y is round(3.5), Z is floor(-3.5), W is ceiling(3.2), writeln(X/Y/Z/W).
:- X is round(2.4), Y is round(-2.5), Z is integer(2.5), writeln(X/Y/Z).
:- X is floor(3), Y is ceiling(3), writeln(X/Y).
:- X is 100000 * 100000, writeln(X).
:- X is 123456789 * 1000, writeln(X).
:- X is 17, Y is X mod 5, writeln(Y).
:- X is 1 - 1, writeln(X).
:- X is 007 + 1, writeln(X).
:- X is 5, Y is X, writeln(Y).
count(N, N) :- !.
count(I, N) :- I1 is I + 1, count(I1, N).
:- count(0, 500), writeln(counted).
gcd(A, 0, A) :- !.
gcd(A, B, G) :- C is A mod B, gcd(B, C, G).
:- gcd(1071, 462, G), writeln(G).
