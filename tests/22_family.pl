parent(tom, bob). parent(tom, liz). parent(bob, ann). parent(bob, pat). parent(pat, jim).
male(tom). male(bob). male(jim).
female(liz). female(ann). female(pat).
grandparent(X, Z) :- parent(X, Y), parent(Y, Z).
sibling(X, Y) :- parent(Z, X), parent(Z, Y), X \= Y.
ancestor(X, Y) :- parent(X, Y).
ancestor(X, Y) :- parent(X, Z), ancestor(Z, Y).
mother(M, C) :- parent(M, C), female(M).
father(F, C) :- parent(F, C), male(F).
:- findall(X-Z, grandparent(X, Z), L), writeln(L).
:- findall(X-Y, sibling(X, Y), L), writeln(L).
:- findall(Y, ancestor(tom, Y), L), writeln(L).
:- findall(X, ancestor(X, jim), L), writeln(L).
:- findall(M, mother(M, _), L), sort(L, S), writeln(S).
:- ( father(tom, bob) -> writeln(yes) ; writeln(no) ).
:- ( mother(tom, bob) -> writeln(yes) ; writeln(no) ).
:- findall(C, parent(bob, C), Cs), length(Cs, N), writeln(bob_has(N)).
depth(X, X, 0).
depth(A, D, N) :- parent(A, C), depth(C, D, N0), N is N0 + 1.
:- depth(tom, jim, N), writeln(N).
:- listing(grandparent).
:- listing(parent).
