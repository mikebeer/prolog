age(peter, 7). age(ann, 11). age(pat, 8). age(tom, 5). age(mike, 11).
:- findall(N, age(N, _), L), writeln(L).
:- findall(N-A, age(N, A), L), writeln(L).
:- findall(N, (age(N, A), A > 7), L), writeln(L).
:- findall(X, fail, L), writeln(L).
:- findall(A, age(_, A), L), sort(L, S), writeln(S).
:- findall(X-Y, (member(X, [1, 2]), member(Y, [a, b])), L), writeln(L).
:- findall(X, member(X, [c, a, b]), L), msort(L, S), writeln(S).
:- findall(X, (between(1, 10, X), X mod 2 =:= 0), L), sum_list(L, S), writeln(L/S).
:- findall(f(X, Y), member(X, [1, 2]), L), writeln(L).
:- findall(L2, findall(X, member(X, [1, 2]), L2), R), writeln(R).
:- findall(X, (member(X, [1, 2, 3]), findall(Y, (member(Y, [1, 2, 3]), Y < X), Ys), length(Ys, 2)), L), writeln(L).
:- setof(X, age(X, _), L), writeln(L).
:- bagof(X, age(X, _), L), writeln(L).
:- aggregate_all(count, age(_, _), C), writeln(C).
count(G, N) :- findall(x, G, L), length(L, N).
:- count(age(_, _), N), writeln(N).
:- count(age(_, 11), N), writeln(N).
sum(G, T, S) :- findall(T, G, L), sum_list(L, S).
:- sum(age(_, A), A, S), writeln(S).
:- findall(X, member(X, [a, b]), [P|Q]), writeln(P/Q).
:- findall(X, member(X, [a, b]), [a]) -> writeln(yes) ; writeln(no).
