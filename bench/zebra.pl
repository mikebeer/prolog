right_of(X, Y, [Y, X|_]).
right_of(X, Y, [_|T]) :- right_of(X, Y, T).
next_to(X, Y, L) :- right_of(X, Y, L).
next_to(X, Y, L) :- right_of(Y, X, L).
zebra(Owner) :-
  Hs = [h(_, norwegian, _, _, _), _, h(_, _, _, milk, _), _, _],
  member(h(red, english, _, _, _), Hs),
  member(h(_, spanish, dog, _, _), Hs),
  member(h(green, _, _, coffee, _), Hs),
  member(h(_, ukrainian, _, tea, _), Hs),
  right_of(h(green, _, _, _, _), h(ivory, _, _, _, _), Hs),
  member(h(_, _, snails, _, oldgold), Hs),
  member(h(yellow, _, _, _, kools), Hs),
  next_to(h(_, _, _, _, chesterfield), h(_, _, fox, _, _), Hs),
  next_to(h(_, _, _, _, kools), h(_, _, horse, _, _), Hs),
  member(h(_, _, _, orange_juice, luckystrike), Hs),
  member(h(_, japanese, _, _, parliament), Hs),
  next_to(h(_, norwegian, _, _, _), h(blue, _, _, _, _), Hs),
  member(h(_, Owner, zebra, _, _), Hs),
  member(h(_, _, _, water, _), Hs).
:- zebra(O), writeln(O).
