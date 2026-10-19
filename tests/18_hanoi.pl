hanoi(0, _, _, _) :- !.
hanoi(N, From, To, Via) :-
  N1 is N - 1,
  hanoi(N1, From, Via, To),
  write(move(N, From, To)), nl,
  hanoi(N1, Via, To, From).
:- hanoi(3, left, right, center).
moves(0, 0) :- !.
moves(N, M) :- N1 is N - 1, moves(N1, M1), M is 2 * M1 + 1.
:- moves(10, M), writeln(M).
:- moves(20, M), writeln(M).
:- moves(60, M), writeln(M).
count_moves(0, 0) :- !.
count_moves(N, C) :- N1 is N - 1, count_moves(N1, C1), C is C1 + 1 + C1.
:- count_moves(12, C), writeln(C).
