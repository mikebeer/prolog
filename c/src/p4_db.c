/* p4_db.c - clause database, clause instantiation, listing, consult */

typedef struct Goal { Term *t; struct Goal *next; } Goal;

static Pred *g_plhead, *g_pltail;
static long g_clcount, g_bootmax;

static Pred *pred_find(Sym *fn, int ar, int create)
{
  Pred *p;
  for (p = fn->preds; p; p = p->next)
    if (p->ar == ar) return p;
  if (!create) return NULL;
  p = (Pred *)xmalloc(sizeof(Pred));
  memset(p, 0, sizeof(Pred));
  p->fn = fn;
  p->ar = ar;
  p->next = fn->preds;
  fn->preds = p;
  return p;
}

static void pred_note_listed(Pred *p)
{
  if (p->listed) return;
  p->listed = 1;
  p->lnext = NULL;
  if (g_pltail) g_pltail->lnext = p;
  else g_plhead = p;
  g_pltail = p;
}

/* ---- first-argument index ----------------------------------------------
   For a predicate with IDX_MIN or more clauses, a call whose first argument is
   an atom, a number or a compound is resolved through a hash table from the
   key (atom symbol / numeric value / functor and arity) to the clauses that can
   match it: those with the same key and those with a variable first argument,
   in clause order.  A bucket is an array of clauses plus their positions in the
   predicate; a running call keeps the pointer and the count it started with, so
   buckets (like the clause arrays) only grow at the end and are never freed.
   pre[i] is the sum of nhead over clauses 0..i-1: clauses skipped by the index
   still advance the variable renaming counter g_rvc exactly as trying them
   would have done (see try_clause in p5_solve.c), so answers and variable
   names are unchanged.
   The index describes one snapshot (v, n) of the predicate; assertz keeps it
   up to date, asserta / retract / reload drop it and it is rebuilt on demand. */
#ifndef IDX_MIN
#define IDX_MIN 8
#endif
#define IDX_MAXWILD 64

typedef struct { Clause **c; int *pos; int n, cap; } IBkt;
typedef struct { int kind; const void *p; double d; int ar; IBkt b; } IEnt;  /* kind 0 = free */
struct PIdx {
  Clause **v;
  int n;
  long *pre;
  int precap;
  IEnt *tab;
  int tcap, tcnt;
  IBkt wild;
  int bad;                 /* too many variable-first-argument clauses: do not use */
};

/* key of a (dereferenced, non-variable) first argument; 0 = no key */
static int idx_key(Term *t, const void **p, double *d, int *ar)
{
  *p = NULL;
  *d = 0.0;
  *ar = 0;
  switch (t->tag) {
  case T_ATOM: case T_QATOM: *p = t->u.a.sym; return 1;
  case T_NUM:
    *d = num_val(t->u.n.txt);
    if (*d == 0.0) *d = 0.0;
    return 2;
  case T_CMP: *p = t->u.c.fn; *ar = t->u.c.ar; return 3;
  default: return 0;
  }
}

static unsigned long idx_hash(int kind, const void *p, double d, int ar)
{
  unsigned long h;
  int e;
  double m;
  if (kind == 2) {
    m = frexp(d, &e);
    h = (unsigned long)(long)(m * 2147483648.0) * 31UL + (unsigned long)(long)(e + 2000);
  } else {
    h = (unsigned long)p;
    h = (h >> 3) * 2654435761UL + (unsigned long)ar * 40503UL;
  }
  return (h ^ (h >> 15)) * 2246822519UL + (unsigned long)kind;
}

static int idx_first_key(Clause *c, const void **p, double *d, int *ar)
{
  Term *a;
  if (c->head->tag != T_CMP || c->head->u.c.ar < 1) return 0;
  a = c->head->u.c.args[0];
  return idx_key(a, p, d, ar);   /* T_TV (variable) gives 0 */
}

static void ibkt_add(IBkt *b, Clause *c, int pos)
{
  if (b->n >= b->cap) {
    int ncap = b->cap ? b->cap * 2 : 2;
    Clause **nc = (Clause **)xmalloc(sizeof(Clause *) * (size_t)ncap);
    int *np = (int *)xmalloc(sizeof(int) * (size_t)ncap);
    if (b->n) {
      memcpy(nc, b->c, sizeof(Clause *) * (size_t)b->n);
      memcpy(np, b->pos, sizeof(int) * (size_t)b->n);
    }
    b->c = nc;
    b->pos = np;
    b->cap = ncap;
  }
  b->c[b->n] = c;
  b->pos[b->n] = pos;
  b->n++;
}

static IEnt *idx_find(PIdx *x, int kind, const void *p, double d, int ar, int create)
{
  unsigned long h;
  int i;
  if (x->tcap == 0 || (create && (x->tcnt + 1) * 2 > x->tcap)) {
    int ncap = x->tcap ? x->tcap * 2 : 16, j;
    IEnt *nt = (IEnt *)xmalloc(sizeof(IEnt) * (size_t)ncap);
    memset(nt, 0, sizeof(IEnt) * (size_t)ncap);
    for (j = 0; j < x->tcap; j++) {
      if (x->tab[j].kind) {
        h = idx_hash(x->tab[j].kind, x->tab[j].p, x->tab[j].d, x->tab[j].ar);
        i = (int)(h & (unsigned long)(ncap - 1));
        while (nt[i].kind) i = (i + 1) & (ncap - 1);
        nt[i] = x->tab[j];
      }
    }
    free(x->tab);   /* entries are copied by value; running calls hold bucket arrays only */
    x->tab = nt;
    x->tcap = ncap;
  }
  h = idx_hash(kind, p, d, ar);
  i = (int)(h & (unsigned long)(x->tcap - 1));
  while (x->tab[i].kind) {
    IEnt *e = &x->tab[i];
    if (e->kind == kind && e->p == p && e->ar == ar && (kind != 2 || e->d == d)) return e;
    i = (i + 1) & (x->tcap - 1);
  }
  if (!create) return NULL;
  x->tab[i].kind = kind;
  x->tab[i].p = p;
  x->tab[i].d = d;
  x->tab[i].ar = ar;
  x->tab[i].b.c = NULL;
  x->tab[i].b.pos = NULL;
  x->tab[i].b.n = x->tab[i].b.cap = 0;
  x->tcnt++;
  return &x->tab[i];
}

/* a new bucket starts as a copy of the variable-first-argument clauses before it */
static void idx_new_bucket(PIdx *x, IEnt *e)
{
  int i;
  for (i = 0; i < x->wild.n; i++) ibkt_add(&e->b, x->wild.c[i], x->wild.pos[i]);
}

/* add clause c at position pos; returns 0 when the index must be dropped */
static int idx_append(PIdx *x, Clause *c, int pos)
{
  const void *p;
  double d;
  int ar, kind, i;
  if (pos + 2 > x->precap) {
    int ncap = x->precap ? x->precap * 2 : 16;
    long *np = (long *)xmalloc(sizeof(long) * (size_t)ncap);
    if (x->precap) memcpy(np, x->pre, sizeof(long) * (size_t)x->precap);
    x->pre = np;
    x->precap = ncap;
  }
  if (pos == 0) x->pre[0] = 0;
  x->pre[pos + 1] = x->pre[pos] + c->nhead;
  kind = idx_first_key(c, &p, &d, &ar);
  if (kind == 0) {
    if (x->wild.n >= IDX_MAXWILD || (long)x->tcnt * (x->wild.n + 1) > 4096L) return 0;
    ibkt_add(&x->wild, c, pos);
    for (i = 0; i < x->tcap; i++)
      if (x->tab[i].kind) ibkt_add(&x->tab[i].b, c, pos);
  } else {
    IEnt *e = idx_find(x, kind, p, d, ar, 0);
    if (e == NULL) {
      e = idx_find(x, kind, p, d, ar, 1);
      idx_new_bucket(x, e);
    }
    ibkt_add(&e->b, c, pos);
  }
  x->n = pos + 1;
  return 1;
}

static PIdx *idx_build(Pred *p)
{
  PIdx *x = (PIdx *)xmalloc(sizeof(PIdx));
  int i;
  memset(x, 0, sizeof(PIdx));
  x->v = p->v;
  for (i = 0; i < p->n; i++) {
    if (!idx_append(x, p->v[i], i)) {
      x->bad = 1;
      break;
    }
  }
  x->n = p->n;
  return x;
}

/* the candidate clauses for a call of p with first argument a (dereferenced);
   NULL when the predicate is too small or the argument is a variable */
static PIdx *idx_for_call(Pred *p, Term *a, IBkt **bk)
{
  const void *kp;
  double d;
  int ar, kind;
  PIdx *x;
  IEnt *e;
  static IBkt none;
  if (p->n < IDX_MIN) return NULL;
  kind = idx_key(a, &kp, &d, &ar);
  if (kind == 0) return NULL;
  x = p->idx;
  if (x == NULL || x->v != p->v || x->n != p->n) {
    x = idx_build(p);
    p->idx = x;
  }
  if (x->bad) return NULL;
  e = idx_find(x, kind, kp, d, ar, 0);
  if (e != NULL) *bk = &e->b;
  else if (x->wild.n > 0) *bk = &x->wild;
  else *bk = &none;
  return x;
}


/* a snapshot of the clause list stays valid while clauses are added */
static void pred_add(Pred *p, Clause *c, int atz)
{
  int keep = atz && p->idx != NULL && !p->idx->bad && p->idx->v == p->v && p->idx->n == p->n;
  pred_note_listed(p);
  if (!keep) p->idx = NULL;
  if (atz) {
    if (p->n < p->cap) {
      p->v[p->n++] = c;
    } else {
      Clause **nv;
      int ncap = p->cap ? p->cap * 2 : 4;
      nv = (Clause **)xmalloc(sizeof(Clause *) * (size_t)ncap);
      if (p->n) memcpy(nv, p->v, sizeof(Clause *) * (size_t)p->n);
      nv[p->n++] = c;
      p->v = nv;
      p->cap = ncap;
    }
  } else {
    Clause **nv = (Clause **)xmalloc(sizeof(Clause *) * (size_t)(p->n + 4));
    if (p->n) memcpy(nv + 1, p->v, sizeof(Clause *) * (size_t)p->n);
    nv[0] = c;
    p->v = nv;
    p->n++;
    p->cap = p->n + 3;
  }
  if (keep) {
    p->idx->v = p->v;
    if (!idx_append(p->idx, c, p->n - 1)) p->idx = NULL;
  }
}

static void pred_set(Pred *p, Clause **keep, int n)
{
  Clause **nv = (Clause **)xmalloc(sizeof(Clause *) * (size_t)(n + 4));
  if (n) memcpy(nv, keep, sizeof(Clause *) * (size_t)n);
  p->v = nv;
  p->n = n;
  p->cap = n + 4;
  p->idx = NULL;
}


/* ---- templates ---- */
typedef struct { Term **v; int n, cap; } VList;

static int vl_index(VList *l, Term *v)
{
  int i;
  for (i = 0; i < l->n; i++)
    if (l->v[i] == v) return i;
  if (l->n >= l->cap) {
    l->cap = l->cap ? l->cap * 2 : 16;
    l->v = (Term **)xrealloc(l->v, sizeof(Term *) * (size_t)l->cap);
  }
  l->v[l->n] = v;
  return l->n++;
}

static int is_gnd(Term *t)
{
  if (t->tag == T_CMP) return t->gnd;
  return t->tag == T_ATOM || t->tag == T_QATOM || t->tag == T_NUM;
}

static Term *tmpl_build(Term *t, VList *vl)
{
  t = deref(t);
  switch (t->tag) {
  case T_VAR: {
    Term *x = (Term *)aalloc(&g_perm, sizeof(Term));
    x->tag = T_TV;
    x->gnd = 0;
    x->u.x.idx = vl_index(vl, t);
    return x;
  }
  case T_NUM: return mk_numa(&g_perm, t->u.n.txt);
  case T_CMP: {
    int i, n = t->u.c.ar, g = 1;
    Term *r = mk_cmpa(&g_perm, t->u.c.fn, n);
    for (i = 0; i < n; i++) {
      r->u.c.args[i] = tmpl_build(t->u.c.args[i], vl);
      if (!is_gnd(r->u.c.args[i])) g = 0;
    }
    r->gnd = g;
    return r;
  }
  default: return t;
  }
}

static void flatten_conj(Term *t, Term ***out, int *n, int *cap)
{
  while (t->tag == T_CMP && t->u.c.fn == S_COMMA && t->u.c.ar == 2) {
    flatten_conj(t->u.c.args[0], out, n, cap);
    t = t->u.c.args[1];
  }
  if (*n >= *cap) {
    *cap = *cap ? *cap * 2 : 8;
    *out = (Term **)xrealloc(*out, sizeof(Term *) * (size_t)*cap);
  }
  (*out)[(*n)++] = t;
}

/* AssertClause: term has been dereferenced fully; atz = 1 appends */
static void assert_clause(Term *term, int atz)
{
  Term *head = term, **goals = NULL;
  int ng = 0, gcap = 0, i, ar;
  VList vl;
  Clause *c;
  Sym *fn;
  Pred *p;
  Buf nb;
  if (term->tag == T_CMP && term->u.c.fn == S_NECK && term->u.c.ar == 2) {
    head = term->u.c.args[0];
    flatten_conj(term->u.c.args[1], &goals, &ng, &gcap);
  }
  fn = functor_of(head, &ar);
  p = pred_find(fn, ar, 1);
  g_clcount++;
  c = (Clause *)xmalloc(sizeof(Clause));
  c->id = g_clcount;
  vl.v = NULL; vl.n = vl.cap = 0;
  c->head = tmpl_build(head, &vl);
  c->nhead = vl.n;
  c->nbody = ng;
  c->body = (Term **)aalloc(&g_perm, sizeof(Term *) * (size_t)(ng ? ng : 1));
  for (i = 0; i < ng; i++) c->body[i] = tmpl_build(goals[i], &vl);
  c->nvars = vl.n;
  c->vn = (char **)aalloc(&g_perm, sizeof(char *) * (size_t)(vl.n ? vl.n : 1));
  nb.s = NULL; nb.n = nb.cap = 0;
  for (i = 0; i < vl.n; i++) {
    bclear(&nb);
    var_name(vl.v[i], &nb);
    c->vn[i] = astrdup(&g_perm, bstr(&nb));
  }
  free(nb.s);
  free(vl.v);
  free(goals);
  pred_add(p, c, atz);
}

/* ---- instantiation ---- */
static Term *inst(Term *t, Term **fr, Clause *c, long cbase)
{
  int i, n;
  Term *r;
  switch (t->tag) {
  case T_TV:
    i = (int)t->u.x.idx;
    if (fr[i] == NULL) fr[i] = mk_var(c->vn[i], '#', cbase + i + 1);
    return fr[i];
  case T_CMP:
    if (t->gnd) return t;
    n = t->u.c.ar;
    r = mk_cmp(t->u.c.fn, n);
    for (i = 0; i < n; i++) r->u.c.args[i] = inst(t->u.c.args[i], fr, c, cbase);
    return r;
  default: return t;
  }
}

static Term *inst_goal(Term *t, Term **fr, Clause *c, long cbase, long barrier)
{
  if (t->tag == T_ATOM && t->u.a.sym == S_BANG) {
    Term *x = (Term *)aalloc(&g_heap, sizeof(Term));
    x->tag = T_CUT;
    x->gnd = 0;
    x->u.x.idx = barrier;
    return x;
  }
  if (t->tag == T_CMP && t->u.c.ar == 2 &&
      (t->u.c.fn == S_COMMA || t->u.c.fn == S_SEMI || t->u.c.fn == S_ARROW)) {
    Term *r = mk_cmp(t->u.c.fn, 2);
    r->u.c.args[0] = inst_goal(t->u.c.args[0], fr, c, cbase, barrier);
    r->u.c.args[1] = inst_goal(t->u.c.args[1], fr, c, cbase, barrier);
    return r;
  }
  return inst(t, fr, c, cbase);
}

/* unify a goal argument with a head template */
static int unify_head(Term *tm, Term *t, Term **fr, Clause *c, long cbase)
{
  Term *d;
  int i, n;
  switch (tm->tag) {
  case T_TV:
    i = (int)tm->u.x.idx;
    if (fr[i] == NULL) {
      d = deref(t);
      if (d->tag == T_VAR) {
        fr[i] = mk_var(c->vn[i], '#', cbase + i + 1);
        bind(d, fr[i]);
      } else {
        fr[i] = d;
      }
      return 1;
    }
    return unify(t, fr[i]);
  case T_CMP:
    d = deref(t);
    if (d->tag == T_VAR) {
      bind(d, inst(tm, fr, c, cbase));
      return 1;
    }
    if (d->tag != T_CMP || d->u.c.fn != tm->u.c.fn || d->u.c.ar != tm->u.c.ar) return 0;
    n = tm->u.c.ar;
    for (i = 0; i < n; i++)
      if (!unify_head(tm->u.c.args[i], d->u.c.args[i], fr, c, cbase)) return 0;
    return 1;
  default:
    d = deref(t);
    if (d->tag == T_VAR) {
      bind(d, tm);
      return 1;
    }
    return unify(d, tm);
  }
}

static Term *conj_term(Term **goals, int n)
{
  Term *r;
  int i;
  if (n == 0) return mk_atom(S_TRUE);
  r = goals[n - 1];
  for (i = n - 2; i >= 0; i--) r = mk_cmp2(S_COMMA, goals[i], r);
  return r;
}

/* retract/1: first clause whose head and body unify */
static int retract_clause(Term *headpat, Term *bodypat)
{
  int ar, i, j;
  Sym *fn = functor_of(headpat, &ar);
  Pred *p = pred_find(fn, ar, 0);
  Clause **v, *c;
  int n;
  if (p == NULL) return 0;
  v = p->v;
  n = p->n;
  for (i = 0; i < n; i++) {
    long tm = trail_mark();
    AMark hm = amark(&g_heap);
    Term **fr, *rh, *rb, **gl;
    c = v[i];
    fr = (Term **)aalloc(&g_heap, sizeof(Term *) * (size_t)(c->nvars + 1));
    for (j = 0; j < c->nvars; j++) fr[j] = NULL;
    rh = inst(c->head, fr, c, g_rvc);
    gl = (Term **)aalloc(&g_heap, sizeof(Term *) * (size_t)(c->nbody + 1));
    for (j = 0; j < c->nbody; j++) gl[j] = inst(c->body[j], fr, c, g_rvc);
    g_rvc += c->nvars;
    rb = conj_term(gl, c->nbody);
    if (unify(headpat, rh) && unify(bodypat, rb)) {
      Clause **keep = (Clause **)xmalloc(sizeof(Clause *) * (size_t)(n + 1));
      int k = 0;
      for (j = 0; j < n; j++) if (v[j] != c) keep[k++] = v[j];
      pred_set(p, keep, k);
      free(keep);
      return 1;
    }
    trail_undo(tm);
    arelease(&g_heap, hm);
  }
  return 0;
}

static void retract_all(Term *pat)
{
  int ar, i, j, n, k = 0;
  Sym *fn = functor_of(pat, &ar);
  Pred *p = pred_find(fn, ar, 1);
  Clause **keep, **v;
  v = p->v;
  n = p->n;
  keep = (Clause **)xmalloc(sizeof(Clause *) * (size_t)(n + 1));
  for (i = 0; i < n; i++) {
    long tm = trail_mark();
    AMark hm = amark(&g_heap);
    Term **fr, *rh;
    Clause *c = v[i];
    fr = (Term **)aalloc(&g_heap, sizeof(Term *) * (size_t)(c->nvars + 1));
    for (j = 0; j < c->nvars; j++) fr[j] = NULL;
    rh = inst(c->head, fr, c, g_rvc);
    g_rvc += c->nhead;
    if (!unify(pat, rh)) keep[k++] = c;
    trail_undo(tm);
    arelease(&g_heap, hm);
  }
  pred_set(p, keep, k);
  free(keep);
}

/* ---- listing and saving ---- */
static void clause_text(Clause *c, Buf *b)
{
  g_wtvn = c->vn;
  wt(c->head, 1, 1200, b);
  if (c->nbody == 0) {
    bputc(b, '.');
  } else {
    AMark hm = amark(&g_heap);
    bput(b, " :-\n    ");
    wt(conj_term(c->body, c->nbody), 1, 1200, b);
    bputc(b, '.');
    arelease(&g_heap, hm);
  }
  g_wtvn = NULL;
}

static void listing_all(int user_only)
{
  Pred *p;
  Buf b;
  int i, shown;
  b.s = NULL; b.n = b.cap = 0;
  for (p = g_plhead; p; p = p->lnext) {
    if (p->n == 0) continue;
    shown = 0;
    for (i = 0; i < p->n; i++) {
      if (user_only && p->v[i]->id <= g_bootmax) continue;
      bclear(&b);
      clause_text(p->v[i], &b);
      say(bstr(&b));
      shown = 1;
    }
    if (shown) say("");
  }
  free(b.s);
}

static void listing_one(const char *name)
{
  Pred *p;
  Buf b, k;
  int i;
  const char *sl;
  b.s = NULL; b.n = b.cap = 0;
  k.s = NULL; k.n = k.cap = 0;
  for (p = g_plhead; p; p = p->lnext) {
    /* the key is name/arity; its name part ends at the first slash */
    bclear(&k);
    sl = strchr(p->fn->text, '/');
    if (sl == NULL) bput(&k, p->fn->text);
    else {
      const char *q;
      for (q = p->fn->text; q < sl; q++) bputc(&k, *q);
    }
    if (rx_cmp(bstr(&k), name) != 0) continue;
    for (i = 0; i < p->n; i++) {
      bclear(&b);
      clause_text(p->v[i], &b);
      say(bstr(&b));
    }
    say("");
  }
  free(b.s);
  free(k.s);
}

static void save_workspace(const char *fname, int user_only)
{
  FILE *f = fopen(fname, "w");
  Pred *p;
  Buf b;
  int i;
  long count = 0;
  char nb[48];
  b.s = NULL; b.n = b.cap = 0;
  if (f == NULL) {
    charout("prolog: cannot write ");
    say(fname);
    return;
  }
  for (p = g_plhead; p; p = p->lnext) {
    for (i = 0; i < p->n; i++) {
      if (user_only && p->v[i]->id <= g_bootmax) continue;
      bclear(&b);
      clause_text(p->v[i], &b);
      fputs(bstr(&b), f);
      fputs("\n\n", f);
      count++;
    }
  }
  fclose(f);
  free(b.s);
  if (count == 0 && user_only) {
    charout("prolog: nothing added this session -- saved an empty file to ");
    say(fname);
  } else {
    sprintf(nb, "%ld", count);
    charout("prolog: saved ");
    charout(nb);
    charout(" clause(s) to ");
    say(fname);
  }
}
