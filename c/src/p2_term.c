/* p2_term.c - terms, bindings, unification, ordering, term output */

static Sym *S_NIL, *S_DOT, *S_COMMA, *S_SEMI, *S_ARROW, *S_NECK, *S_TRUE, *S_FAIL,
           *S_FALSE, *S_BANG, *S_NOTP, *S_NOT, *S_CALL, *S_MINUS, *S_CUT, *S_QUERY,
           *S_PIPE, *S_EMPTY, *S_MSORT, *S_SORT;

static Term **g_trail;
static long g_tn, g_tcap;
static long g_rvc;           /* RVARCOUNTER: fresh variable counter */
static long g_pvc;           /* PVARCOUNTER: parsed variable counter */
static char **g_wtvn;        /* variable names while writing a clause template */

/* ---- construction ---- */
static Term *mk_atom(Sym *y)
{
  if (y->pt == NULL) {
    y->pt = (Term *)aalloc(&g_perm, sizeof(Term));
    memset(y->pt, 0, sizeof(Term));
    y->pt->tag = T_ATOM;
    y->pt->u.a.sym = y;
  }
  return y->pt;
}

static Term *mk_qatom(Sym *y)
{
  if (y->qt == NULL) {
    y->qt = (Term *)aalloc(&g_perm, sizeof(Term));
    memset(y->qt, 0, sizeof(Term));
    y->qt->tag = T_QATOM;
    y->qt->u.a.sym = y;
  }
  return y->qt;
}

static Term *mk_qatom_s(const char *s) { return mk_qatom(intern(s)); }
static Term *mk_atom_s(const char *s) { return mk_atom(intern(s)); }

static Term *mk_var(const char *base, int sep, long ctr)
{
  long b = apos(&g_heap);
  Term *t = (Term *)aalloc(&g_heap, sizeof(Term));
  t->tag = T_VAR;
  t->gnd = 0;
  t->u.v.ref = NULL;
  t->u.v.base = base;
  t->u.v.sep = sep;
  t->u.v.ctr = ctr;
  t->u.v.born = b;
  return t;
}

static Term *mk_numa(Arena *a, const char *txt)
{
  Term *t = (Term *)aalloc(a, sizeof(Term));
  t->tag = T_NUM;
  t->gnd = 1;
  t->u.n.txt = astrdup(a, txt);
  return t;
}

static Term *mk_num(const char *txt) { return mk_numa(&g_heap, txt); }

static Term *mk_long(long v)
{
  char b[40];
  sprintf(b, "%ld", v);
  return mk_num(b);
}

static Term *mk_cmpa(Arena *a, Sym *fn, int ar)
{
  Term *t = (Term *)aalloc(a, sizeof(Term) + sizeof(Term *) * (size_t)(ar ? ar : 1));
  int i;
  t->tag = T_CMP;
  t->gnd = 0;
  t->u.c.fn = fn;
  t->u.c.ar = ar;
  t->u.c.args = (Term **)(t + 1);
  for (i = 0; i < ar; i++) t->u.c.args[i] = NULL;
  return t;
}

static Term *mk_cmp(Sym *fn, int ar) { return mk_cmpa(&g_heap, fn, ar); }

static Term *mk_cmp2(Sym *fn, Term *a, Term *b)
{
  Term *t = mk_cmp(fn, 2);
  t->u.c.args[0] = a;
  t->u.c.args[1] = b;
  return t;
}

static Term *mk_cons(Term *h, Term *t) { return mk_cmp2(S_DOT, h, t); }

/* an atom or number from a result text, as unification sees it */
static Term *mk_from_text(const char *s)
{
  if (is_number(s)) return mk_num(s);
  return mk_atom_s(s);
}

static Term *deref(Term *t)
{
  while (t->tag == T_VAR && t->u.v.ref != NULL) t = t->u.v.ref;
  return t;
}

#define IS_CMP(t) ((t)->tag == T_CMP)
#define IS_VAR(t) ((t)->tag == T_VAR)

/* ---- trail ---- */
static void trail_push(Term *v)
{
  if (g_tn >= g_tcap) {
    g_tcap = g_tcap ? g_tcap * 2 : 4096;
    g_trail = (Term **)xrealloc(g_trail, sizeof(Term *) * (size_t)g_tcap);
  }
  g_trail[g_tn++] = v;
}

static long trail_mark(void) { return g_tn; }

static void trail_undo(long mark)
{
  while (g_tn > mark) {
    g_tn--;
    g_trail[g_tn]->u.v.ref = NULL;
  }
}

static void bind(Term *v, Term *t)
{
  v->u.v.ref = t;
  trail_push(v);
}

/* ---- names and text ---- */
static void var_name(Term *v, Buf *b)
{
  char nb[40];
  bput(b, v->u.v.base);
  if (v->u.v.ctr >= 0) {
    if (v->u.v.sep) bputc(b, v->u.v.sep);
    sprintf(nb, "%ld", v->u.v.ctr);
    bput(b, nb);
  }
}

static void enc_term(Term *t, Buf *b)
{
  int i;
  switch (t->tag) {
  case T_VAR:
    bputc(b, 2);
    var_name(t, b);
    break;
  case T_QATOM: {
    const char *p;
    bputc(b, 3);
    for (p = t->u.a.sym->text; *p; p++) bputc(b, *p == ' ' ? 1 : *p);
    break;
  }
  case T_ATOM: bput(b, t->u.a.sym->text); break;
  case T_NUM: bput(b, t->u.n.txt); break;
  case T_CMP:
    bputc(b, 4);
    bput(b, t->u.c.fn->text);
    for (i = 0; i < t->u.c.ar; i++) {
      bputc(b, 6);
      enc_term(t->u.c.args[i], b);
    }
    bputc(b, 7);
    break;
  default: break;
  }
}

/* AtomText: printable text of an atom or number; the internal encoding for others */
static const char *atext(Term *t, Buf *scratch)
{
  switch (t->tag) {
  case T_ATOM:
  case T_QATOM: return t->u.a.sym->text;
  case T_NUM: return t->u.n.txt;
  default:
    bclear(scratch);
    enc_term(t, scratch);
    return bstr(scratch);
  }
}

/* the functor text of a term (atoms count as arity 0) */
static Sym *functor_of(Term *t, int *ar)
{
  Buf b;
  Sym *y;
  switch (t->tag) {
  case T_CMP: *ar = t->u.c.ar; return t->u.c.fn;
  case T_ATOM:
  case T_QATOM: *ar = 0; return t->u.a.sym;
  default:
    b.s = NULL; b.n = 0; b.cap = 0;
    y = intern(atext(t, &b));
    free(b.s);
    *ar = 0;
    return y;
  }
}

/* ---- numbers in unification and ordering ---- */
static int num_eq(Term *a, Term *b)
{
  if (strcmp(a->u.n.txt, b->u.n.txt) == 0) return 1;
  return num_val(a->u.n.txt) == num_val(b->u.n.txt);
}

static int unify(Term *a, Term *b)
{
  for (;;) {
    a = deref(a);
    b = deref(b);
    if (a->tag == T_VAR) {
      if (b == a) return 1;
      bind(a, b);
      return 1;
    }
    if (b->tag == T_VAR) {
      bind(b, a);
      return 1;
    }
    if (a->tag == T_CMP || b->tag == T_CMP) {
      int i, n;
      if (a->tag != T_CMP || b->tag != T_CMP) return 0;
      if (a->u.c.fn != b->u.c.fn || a->u.c.ar != b->u.c.ar) return 0;
      n = a->u.c.ar;
      if (n == 0) return 1;
      for (i = 0; i < n - 1; i++)
        if (!unify(a->u.c.args[i], b->u.c.args[i])) return 0;
      a = a->u.c.args[n - 1];
      b = b->u.c.args[n - 1];
      continue;
    }
    if (a == b) return 1;
    if (a->tag == T_NUM && b->tag == T_NUM) return num_eq(a, b);
    return 0;
  }
}

static int term_kind(Term *t)
{
  switch (t->tag) {
  case T_VAR: return 0;
  case T_NUM: return 1;
  case T_CMP: return 3;
  default: return 2;
  }
}

static int term_compare(Term *a, Term *b)
{
  int k1, k2, i, c;
  Buf s1, s2;
  for (;;) {
    a = deref(a);
    b = deref(b);
    k1 = term_kind(a);
    k2 = term_kind(b);
    if (k1 != k2) return k1 < k2 ? -1 : 1;
    switch (k1) {
    case 0: {
      s1.s = NULL; s1.n = s1.cap = 0;
      s2.s = NULL; s2.n = s2.cap = 0;
      if (a == b) return 0;
      var_name(a, &s1);
      var_name(b, &s2);
      c = rx_cmp(bstr(&s1), bstr(&s2));
      free(s1.s);
      free(s2.s);
      return c;
    }
    case 1: {
      double x = num_val(a->u.n.txt), y = num_val(b->u.n.txt);
      if (strcmp(a->u.n.txt, b->u.n.txt) == 0) return 0;
      return x < y ? -1 : (x > y ? 1 : 0);
    }
    case 2:
      if (a == b) return 0;
      return rx_cmp(a->u.a.sym->text, b->u.a.sym->text);
    default: {
      int n;
      if (a->u.c.ar != b->u.c.ar) return a->u.c.ar < b->u.c.ar ? -1 : 1;
      if (a->u.c.fn != b->u.c.fn) {
        c = rx_cmp(a->u.c.fn->text, b->u.c.fn->text);
        if (c != 0) return c;
      }
      n = a->u.c.ar;
      if (n == 0) return 0;
      for (i = 0; i < n - 1; i++) {
        c = term_compare(a->u.c.args[i], b->u.c.args[i]);
        if (c != 0) return c;
      }
      a = a->u.c.args[n - 1];
      b = b->u.c.args[n - 1];
      continue;
    }
    }
  }
}

/* ---- full dereferencing copy ---- */
static Term *deref_full(Term *t)
{
  Term *first = NULL, **hole = &first, *r;
  for (;;) {
    t = deref(t);
    if (t->tag != T_CMP || t->u.c.ar == 0) {
      *hole = t;
      return first;
    }
    {
      int i, n = t->u.c.ar;
      r = mk_cmp(t->u.c.fn, n);
      for (i = 0; i < n - 1; i++) r->u.c.args[i] = deref_full(t->u.c.args[i]);
      *hole = r;
      hole = &r->u.c.args[n - 1];
      t = t->u.c.args[n - 1];
    }
  }
}

/* ---- term output ---- */
static int wt_needs_quote(const char *text)
{
  const char *p;
  if (strcmp(text, "[]") == 0 || strcmp(text, "!") == 0 || strcmp(text, ";") == 0 ||
      strcmp(text, "{}") == 0) return 0;
  if (*text == '\0') return 1;
  if (is_lower_c(*text)) {
    for (p = text; *p; p++)
      if (!is_ident_c(*p)) return 1;
    return 0;
  }
  for (p = text; *p; p++)
    if (!is_sym_c(*p)) return 1;
  return 0;
}

static void wt_atom(const char *text, int quoted, Buf *b)
{
  const char *p;
  if (!quoted || !wt_needs_quote(text)) {
    bput(b, text);
    return;
  }
  bputc(b, '\'');
  for (p = text; *p; p++) {
    if (*p == '\'') bputc(b, '\'');
    bputc(b, *p);
  }
  bputc(b, '\'');
}

static void wt(Term *t, int quoted, int maxprec, Buf *b);

static void wt_list(Term *t, int quoted, Buf *b)
{
  Term *rest, *rd;
  bputc(b, '[');
  wt(t->u.c.args[0], quoted, 999, b);
  rest = t->u.c.args[1];
  for (;;) {
    rd = rest;
    if (rd->tag == T_VAR) rd = deref(rd);
    if (rd == S_NIL->pt) {
      rest = rd;
      break;
    }
    if (rd->tag == T_CMP && rd->u.c.fn == S_DOT && rd->u.c.ar == 2) {
      bputc(b, ',');
      wt(rd->u.c.args[0], quoted, 999, b);
      rest = rd->u.c.args[1];
      continue;
    }
    rest = rd;
    break;
  }
  if (rest != S_NIL->pt) {
    bputc(b, '|');
    wt(rest, quoted, 999, b);
  }
  bputc(b, ']');
}

static void wt(Term *t, int quoted, int maxprec, Buf *b)
{
  Sym *fn;
  int ar, i, prec, lp, rp;
  Buf tmp;
  switch (t->tag) {
  case T_VAR:
    bputc(b, '_');
    var_name(t, b);
    return;
  case T_TV:
    bputc(b, '_');
    bput(b, g_wtvn ? g_wtvn[t->u.x.idx] : "?");
    return;
  case T_NUM:
    bput(b, t->u.n.txt);
    return;
  case T_ATOM:
  case T_QATOM:
    wt_atom(t->u.a.sym->text, quoted, b);
    return;
  case T_CUT: {
    char nb[48];
    sprintf(nb, "$cut(%ld)", t->u.x.idx);
    bput(b, nb);
    return;
  }
  default: break;
  }
  fn = t->u.c.fn;
  ar = t->u.c.ar;
  if (fn == S_DOT && ar == 2) {
    wt_list(t, quoted, b);
    return;
  }
  if (ar == 2 && fn->ip > 0) {
    prec = fn->ip;
    lp = prec - 1;
    rp = prec - 1;
    if (fn->ia == 'r') rp = prec;
    if (fn->ia == 'l') lp = prec;
    tmp.s = NULL; tmp.n = tmp.cap = 0;
    wt(t->u.c.args[0], quoted, lp, &tmp);
    if (fn == S_COMMA) bput(&tmp, ",");
    else {
      bputc(&tmp, ' ');
      bput(&tmp, fn->text);
      bputc(&tmp, ' ');
    }
    wt(t->u.c.args[1], quoted, rp, &tmp);
    if (prec > maxprec) bputc(b, '(');
    bput(b, bstr(&tmp));
    if (prec > maxprec) bputc(b, ')');
    free(tmp.s);
    return;
  }
  if (ar == 1 && fn->pp > 0) {
    prec = fn->pp;
    rp = prec - 1;
    if (fn->pa == 'y') rp = prec;
    if (prec > maxprec) bputc(b, '(');
    bput(b, fn->text);
    bputc(b, ' ');
    wt(t->u.c.args[0], quoted, rp, b);
    if (prec > maxprec) bputc(b, ')');
    return;
  }
  wt_atom(fn->text, quoted, b);
  bputc(b, '(');
  for (i = 0; i < ar; i++) {
    if (i > 0) bputc(b, ',');
    wt(t->u.c.args[i], quoted, 999, b);
  }
  bputc(b, ')');
}

/* WriteTerm: the text lives in a static buffer until the next call */
static Buf g_wbuf;

static const char *write_term(Term *t, int quoted)
{
  bclear(&g_wbuf);
  wt(t, quoted, 1200, &g_wbuf);
  return bstr(&g_wbuf);
}
