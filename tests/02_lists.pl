:- append([1, 2], [3, 4], L), writeln(L).
:- findall(X-Y, append(X, Y, [a, b, c]), L), writeln(L).
:- member(X, [a, b, c]), writeln(X), fail ; true.
:- ( member(z, [a, b]) -> writeln(found) ; writeln(missing) ).
:- length([a, b, c], N), writeln(N).
:- length(L, 3), writeln(L).
:- reverse([1, 2, 3, 4], R), writeln(R).
:- nth0(1, [a, b, c], X), nth1(1, [a, b, c], Y), writeln(X/Y).
:- last([1, 2, 3], X), writeln(X).
:- sum_list([1, 2, 3, 4], S), max_list([3, 9, 2], Mx), min_list([3, 9, 2], Mn), writeln(S/Mx/Mn).
:- select(b, [a, b, c], R), writeln(R).
:- findall(P, permutation([1, 2, 3], P), L), writeln(L).
:- delete([a, b, a, c], a, R), writeln(R).
:- msort([c, a, b, a], L), writeln(L).
:- sort([c, a, b, a], L), writeln(L).
:- sort([3, 1, 2, 1], L), writeln(L).
double(X, Y) :- Y is X * 2.
positive(X) :- X > 0.
:- maplist(double, [1, 2, 3], L), writeln(L).
:- include(positive, [-1, 2, -3, 4], L), writeln(L).
:- exclude(positive, [-1, 2, -3, 4], L), writeln(L).
:- findall(X, between(1, 5, X), L), writeln(L).
:- forall(member(X, [1, 2, 3]), X > 0), writeln(all_positive).
:- ( forall(member(X, [1, -2, 3]), X > 0) -> writeln(all_positive) ; writeln(not_all) ).
:- concat_atom([a, b, c], R), writeln(R).
:- selectchk(a, [a, b, a], R), writeln(R).
:- append(X, [c], [a, b, c]), writeln(X).
:- length(L, N), N >= 2, !, writeln(L/N).
