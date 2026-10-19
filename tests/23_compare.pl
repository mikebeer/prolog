:- compare(O, 1, 2), writeln(O).
:- compare(O, b, a), writeln(O).
:- compare(O, f(x), f(x)), writeln(O).
:- compare(O, 1, a), writeln(O).
:- compare(O, a, f(x)), writeln(O).
:- compare(O, f(a), g), writeln(O).
:- compare(O, f(a, b), g(a)), writeln(O).
:- compare(O, 1.0, 1), writeln(O).
:- compare(O, 2, 10), writeln(O).
:- compare(O, abc, abd), writeln(O).
:- ( a @< b -> writeln(lt) ; writeln(ge) ).
:- ( f(b) @> f(a) -> writeln(gt) ; writeln(le) ).
:- ( 1 @< a -> writeln(num_before_atom) ; writeln(no) ).
:- ( a @< f(a) -> writeln(atom_before_compound) ; writeln(no) ).
:- ( _ @< 1 -> writeln(var_first) ; writeln(no) ).
:- msort([f(b), a, 3, g(a, b), 1.5, c, f(a), "s"], L), writeln(L).
:- sort([b, a, c, a, b], L), writeln(L).
:- msort([b, a, c, a, b], L), writeln(L).
:- sort([3, 1, 2, 3, 1], L), writeln(L).
:- sort([[b], [a, c], [a]], L), writeln(L).
:- sort([f(2), f(1), f(2)], L), writeln(L).
:- sort([], L), writeln(L).
:- findall(X-Y, (member(X, [2, 1]), member(Y, [b, a])), L), sort(L, S), writeln(S).
:- X = f(A, B), msort([B, A, c], L), length(L, N), writeln(N).
