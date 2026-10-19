% doubly recursive fibonacci
fib(0, 0).
fib(1, 1).
fib(N, F) :- N > 1, A is N - 1, B is N - 2, fib(A, FA), fib(B, FB), F is FA + FB.
:- fib(15, F), write(F), nl.
