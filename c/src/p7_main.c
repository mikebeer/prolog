/* p7_main.c - start-up, session commands, top level, main */

static const char *const g_boot[] = {
  "append([], L, L).",
  "append([H|T], L, [H|R]) :- append(T, L, R).",
  "member(X, [X|_]).",
  "member(X, [_|T]) :- member(X, T).",
  "length([], 0).",
  "length([_|T], N) :- length(T, N0), N is N0 + 1.",
  "reverse(L, R) :- reverse_(L, [], R).",
  "reverse_([], A, A).",
  "reverse_([H|T], A, R) :- reverse_(T, [H|A], R).",
  "last([X], X) :- !.",
  "last([_|T], X) :- last(T, X).",
  "nth0(0, [X|_], X) :- !.",
  "nth0(N, [_|T], X) :- integer(N), N > 0, N1 is N - 1, nth0(N1, T, X).",
  "nth1(1, [X|_], X) :- !.",
  "nth1(N, [_|T], X) :- integer(N), N > 1, N1 is N - 1, nth1(N1, T, X).",
  "sum_list([], 0).",
  "sum_list([H|T], S) :- sum_list(T, S0), S is S0 + H.",
  "max_list([X], X) :- !.",
  "max_list([H|T], M) :- max_list(T, M0), (H >= M0 -> M = H ; M = M0).",
  "min_list([X], X) :- !.",
  "min_list([H|T], M) :- min_list(T, M0), (H =< M0 -> M = H ; M = M0).",
  "between(L, H, L) :- L =< H.",
  "between(L, H, X) :- L < H, L1 is L + 1, between(L1, H, X).",
  "forall(Cond, Action) :- \\+ (Cond, \\+ Action).",
  "select(X, [X|T], T).",
  "select(X, [H|T], [H|R]) :- select(X, T, R).",
  "selectchk(X, L, R) :- select(X, L, R), !.",
  "permutation([], []).",
  "permutation(L, [H|T]) :- select(H, L, R), permutation(R, T).",
  "delete([], _, []).",
  "delete([X|T], X, R) :- !, delete(T, X, R).",
  "delete([H|T], X, [H|R]) :- delete(T, X, R).",
  "exclude(_, [], []).",
  "exclude(P, [H|T], R) :- (call(P, H) -> exclude(P, T, R) ; R = [H|R1], exclude(P, T, R1)).",
  "include(_, [], []).",
  "include(P, [H|T], R) :- (call(P, H) -> R = [H|R1], include(P, T, R1) ; include(P, T, R)).",
  "maplist(_, []).",
  "maplist(P, [H|T]) :- call(P, H), maplist(P, T).",
  "maplist(_, [], []).",
  "maplist(P, [H|T], [H2|T2]) :- call(P, H, H2), maplist(P, T, T2).",
  "maplist(_, [], [], []).",
  "maplist(P, [H|T], [H2|T2], [H3|T3]) :- call(P, H, H2, H3), maplist(P, T, T2, T3).",
  "msort_insert(X, [], [X]).",
  "msort_insert(X, [H|T], [X,H|T]) :- X @=< H, !.",
  "msort_insert(X, [H|T], [H|R]) :- msort_insert(X, T, R).",
  "msort([], []).",
  "msort([H|T], S) :- msort(T, S0), msort_insert(H, S0, S).",
  "sort(L, S) :- msort(L, S0), dedupe_(S0, S).",
  "dedupe_([], []).",
  "dedupe_([X], [X]) :- !.",
  "dedupe_([X,Y|T], R) :- X == Y, !, dedupe_([Y|T], R).",
  "dedupe_([X,Y|T], [X|R]) :- dedupe_([Y|T], R).",
  "concat_atom([], '').",
  "concat_atom([A], A) :- !.",
  "concat_atom([A|T], R) :- concat_atom(T, R0), atom_concat(A, R0, R).",
  NULL
};

static void init_engine(void)
{
  Pred *p;
  int i;
  Buf b;
  /* a fresh session: tokens, variables, trail, clause database */
  tok_clear();
  vmap_clear(&g_qvarmap);
  vmap_clear(&g_pvarmap);
  g_lastterm = '.';
  g_pvc = 0;
  g_tn = 0;
  g_sp = 0;
  g_mode = M_FIRST;
  g_cutsig = 0;
  g_callcount = 0;
  g_fa = NULL;
  g_fatmpl = NULL;
  if (g_heap.n > 0) {
    AMark z;
    z.blk = 0;
    z.off = 0;
    arelease(&g_heap, z);
  }
  for (i = 0; i < NSYMH; i++) {
    Sym *y;
    for (y = g_symtab[i]; y; y = y->next)
      for (p = y->preds; p; p = p->next) {
        p->n = 0;
        p->listed = 0;
        p->lnext = NULL;
      }
  }
  g_plhead = g_pltail = NULL;
  g_clcount = 0;
  b.s = NULL; b.n = b.cap = 0;
  for (i = 0; g_boot[i]; i++) {
    if (i > 0) bputc(&b, '\n');
    bput(&b, g_boot[i]);
  }
  consult_string(bstr(&b));
  free(b.s);
  g_bootmax = g_clcount;
}

/* ---- standard input, one line at a time ---- */
static int stdin_empty(void)
{
  int c = getc(stdin);
  if (c == EOF) return 1;
  ungetc(c, stdin);
  return 0;
}

static char *read_line(void)
{
  Buf b;
  int c;
  b.s = NULL; b.n = b.cap = 0;
  bputc(&b, 'x');
  b.n = 0;
  b.s[0] = '\0';
  while ((c = getc(stdin)) != EOF && c != '\n') bputc(&b, c);
  return b.s;
}

/* STRIP of Rexx: blanks only */
static char *strip_blanks(char *s)
{
  size_t n;
  while (*s == ' ') s++;
  n = strlen(s);
  while (n > 0 && s[n - 1] == ' ') s[--n] = '\0';
  return s;
}

static void sys_help(void)
{
  static const char *const h[] = {
    "session commands:",
    "  )listing      list what YOU added this session (not the",
    "                built-in library -- see listing_all below)",
    "  )lib [dir]    list .pl files available to consult (current",
    "                directory, or [dir] if given)",
    "  )clear        wipe the database back to a fresh session",
    "                (the built-in library is reloaded, your own",
    "                facts/rules/assertions are gone)",
    "  )help  or )?  show this message",
    "  )quit )exit   end the session",
    "",
    "the terminator says what you mean:",
    "  end a QUERY with ?   -- asks whether it is true / for a value:",
    "    female(ann)?",
    "    parent(tom, X)?",
    "  end a FACT or RULE with .  -- adds it to the database:",
    "    female(ann).",
    "    grandparent(X,Z) :- parent(X,Y), parent(Y,Z).",
    "  ?- goal.  and  :- goal.  (SWI/file-directive style, ending in .) "
    "also work and are treated as queries, for anything pasted in from "
    "elsewhere.",
    "",
    "exporting/reloading what you built this session:",
    "  save(File)?          save what YOU added (not the built-in library) "
    "as loadable Prolog source",
    "  save_all(File)?      save EVERYTHING, built-in library included -- "
    "a fully self-contained file",
    "  consult(File)?       load clauses/directives from a file (also: load(File)?)",
    "",
    "other goals, typed as a query:",
    "  listing?             list what you added this session",
    "  listing_all?         list the whole database, library included",
    "  listing(Name)?       list clauses for Name (any arity), library or not",
    "  halt?                end the session (or just: halt.)",
    NULL
  };
  int i;
  for (i = 0; h[i]; i++) say(h[i]);
}

static void sys_lib(const char *dir)
{
#ifdef PL_POSIX
  DIR *d;
  struct dirent *e;
  int found = 0;
  const char *lookin = *dir ? dir : ".";
  d = opendir(lookin);
  if (d == NULL) {
    charout("prolog: )lib -- cannot read directory: ");
    say(lookin);
    return;
  }
  while ((e = readdir(d)) != NULL) {
    size_t n = strlen(e->d_name);
    if (n > 3 && strcmp(e->d_name + n - 3, ".pl") == 0) {
      charout("   ");
      say(e->d_name);
      found++;
    }
  }
  closedir(d);
  if (!found) {
    charout("prolog: )lib -- no .pl files in ");
    say(lookin);
  }
#else
  charout("prolog: )lib -- cannot read directory: ");
  say(*dir ? dir : ".");
#endif
}

/* returns 1 for )quit */
static int handle_syscmd(char *ln)
{
  char *cmd = ln, *rest;
  size_t i;
  char up[32];
  while (*ln && *ln != ' ') ln++;
  if (*ln) {
    *ln++ = '\0';
    while (*ln == ' ') ln++;
  }
  rest = ln;
  for (i = 0; cmd[i] && i < sizeof up - 1; i++)
    up[i] = (char)(strchr(S_LOWER, cmd[i]) ? S_UPPER[strchr(S_LOWER, cmd[i]) - S_LOWER] : cmd[i]);
  up[i] = '\0';
  if (strcmp(up, ")LISTING") == 0) listing_all(1);
  else if (strcmp(up, ")CLEAR") == 0) {
    init_engine();
    say("prolog: workspace cleared.");
  } else if (strcmp(up, ")LIB") == 0) sys_lib(strip_blanks(rest));
  else if (strcmp(up, ")HELP") == 0 || strcmp(up, ")?") == 0) sys_help();
  else if (strcmp(up, ")QUIT") == 0 || strcmp(up, ")EXIT") == 0) return 1;
  else {
    charout("prolog: unknown session command: ");
    charout(cmd);
    say(" (try )help)");
  }
  return 0;
}

/* ---- top level ---- */
static int interact_on_solution(void)
{
  Buf line;
  int i;
  char *ans, *a;
  if (g_qvarmap.n == 0) {
    say("true.");
  } else {
    line.s = NULL; line.n = line.cap = 0;
    for (i = 0; i < g_qvarmap.n; i++) {
      if (i > 0) bput(&line, ", ");
      bput(&line, g_qvarmap.e[i].name);
      bput(&line, " = ");
      bput(&line, write_term(deref_full(g_qvarmap.e[i].var), 1));
    }
    say(bstr(&line));
    free(line.s);
  }
  charout("     more (;)? ");
  fflush(stdout);
  if (stdin_empty()) return 1;
  ans = read_line();
  a = strip_blanks(ans);
  i = strcmp(a, ";") == 0;
  free(ans);
  return i ? 0 : 1;
}

static void solve_interact_show(Term *g)
{
  int ok;
  long mark;
  vmap_copy(&g_qvarmap, &g_pvarmap);
  g_mode = M_INTERACT;
  mark = trail_mark();
  ok = run(new_goal(g, NULL));
  g_mode = M_FIRST;
  if (!ok) say("false.");
  trail_undo(mark);
}

static int query_prefixed(Term *t)
{
  return t->tag == T_CMP && t->u.c.ar == 1 &&
         (t->u.c.fn == S_QUERY || t->u.c.fn == S_NECK);
}

static int reserved_name(Term *t)
{
  int ar;
  Sym *fn = functor_of(t, &ar);
  if (t->tag != T_CMP) {
    if (fn == S_TRUE || fn == S_FAIL || fn == S_FALSE || fn == S_BANG) return 1;
    return is_builtin(fn, 0);
  }
  return is_builtin(fn, ar);
}

static void run_top_level(void)
{
  char *raw, *ln;
  say("Prolog-in-CREXX -- type )help for session commands, halt. to quit");
  say("(end a QUERY with ? -- e.g. female(ann)?  or parent(tom,X)? -- end a "
      "FACT or RULE with . to add it -- e.g. female(ann). or foo(X):-bar(X).)");
  for (;;) {
    AMark hm;
    say("?-");
    if (stdin_empty()) {
      say("");
      say("goodbye.");
      return;
    }
    raw = read_line();
    ln = strip_blanks(raw);
    if (*ln == '\0') { free(raw); continue; }
    if (*ln == ')') {
      int q = handle_syscmd(ln);
      free(raw);
      if (q) {
        say("goodbye.");
        return;
      }
      continue;
    }
    hm = amark(&g_heap);
    tokenize(ln);
    while (g_ppos < g_ntok) {
      Term *goal = parse_clause();
      if (goal == NULL) break;
      if (g_lastterm == '?') {
        solve_interact_show(query_prefixed(goal) ? goal->u.c.args[0] : goal);
      } else if (query_prefixed(goal)) {
        solve_interact_show(goal->u.c.args[0]);
      } else if (reserved_name(goal)) {
        solve_interact_show(goal);
      } else {
        assert_clause(goal, 1);
        charout("%");
        charout("  added: ");
        say(write_term(goal, 1));
      }
    }
    free(raw);
    arelease(&g_heap, hm);
  }
}

int main(int argc, char **argv)
{
  init_syms();
  init_ops();
  init_builtins();
  init_engine();
  if (argc > 1 && argv[1][0] != '\0') {
    FILE *f = fopen(argv[1], "r");
    if (f == NULL) {
      charout("prolog: cannot open source file: ");
      say(argv[1]);
      return 1;
    }
    fclose(f);
    consult_file(argv[1]);
  }
  run_top_level();
  fflush(stdout);
  return 0;
}
