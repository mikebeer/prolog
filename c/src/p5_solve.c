/* p5_solve.c - arithmetic and the resolution engine */

/* ---- arithmetic values: a double plus the text CREXX would hold ---- */
typedef struct { double d; const char *lit; Term *src; char buf[48]; } AV;

static void av_make(AV *r, double x)
{
  fmt_double(x, r->buf);
  r->lit = NULL;
  r->src = NULL;
  if (x != x || x - x != 0.0) r->d = x;
  else r->d = strtod(r->buf, NULL);
}

static const char *av_text(const AV *a)
{
  return a->lit ? a->lit : a->buf;
}

static void av_text_in(AV *r, const char *txt)
{
  r->lit = txt;
  r->src = NULL;
  r->d = num_val(txt);
  r->buf[0] = '\0';
}

static int arith_err(const char *msg, const char *arg)
{
  charout("prolog: arithmetic: ");
  charout(msg);
  if (arg) {
    charout(" ");
    charout(arg);
  }
  charout("\n");
  return 0;
}

static int arith_eval(Term *t, AV *r);

static int arith_fn1(Sym *fn, AV *a, AV *r)
{
  const char *f = fn->text;
  char *tt;
  if (strcmp(f, "-") == 0) { av_make(r, 0.0 - a->d); return 1; }
  if (strcmp(f, "+") == 0) { *r = *a; return 1; }
  if (strcmp(f, "abs") == 0) { av_text_in(r, abs_text(av_text(a))); return 1; }
  if (strcmp(f, "sign") == 0) {
    av_make(r, a->d > 0 ? 1.0 : (a->d < 0 ? -1.0 : 0.0));
    return 1;
  }
  if (strcmp(f, "sqrt") == 0) {
    if (a->d < 0) return arith_err("sqrt of a negative number", NULL);
    av_make(r, sqrt(a->d));
    return 1;
  }
  if (strcmp(f, "truncate") == 0 || strcmp(f, "integer") == 0) {
    av_text_in(r, trunc_text(av_text(a)));
    return 1;
  }
  if (strcmp(f, "float") == 0) { *r = *a; return 1; }
  if (strcmp(f, "round") == 0) { av_text_in(r, round_text(av_text(a))); return 1; }
  if (strcmp(f, "floor") == 0) {
    double neg = 0.0;
    tt = trunc_text(av_text(a));
    if (a->d < 0 && a->d != num_val(tt)) neg = 1.0;
    av_make(r, num_val(tt) - neg);
    return 1;
  }
  if (strcmp(f, "ceiling") == 0) {
    double pos = 0.0;
    tt = trunc_text(av_text(a));
    if (a->d > 0 && a->d != num_val(tt)) pos = 1.0;
    av_make(r, num_val(tt) + pos);
    return 1;
  }
  {
    Buf b;
    char nb[24];
    b.s = NULL; b.n = b.cap = 0;
    bput(&b, f);
    sprintf(nb, "/%d", 1);
    bput(&b, nb);
    arith_err("unknown function", bstr(&b));
    free(b.s);
  }
  return 0;
}

static int arith_fn2(Sym *fn, AV *a, AV *b, AV *r)
{
  const char *f = fn->text;
  if (strcmp(f, "+") == 0) { av_make(r, a->d + b->d); return 1; }
  if (strcmp(f, "-") == 0) { av_make(r, a->d - b->d); return 1; }
  if (strcmp(f, "*") == 0) { av_make(r, a->d * b->d); return 1; }
  if (strcmp(f, "/") == 0) {
    if (b->d == 0.0) return arith_err("division by zero", NULL);
    av_make(r, a->d / b->d);
    return 1;
  }
  if (strcmp(f, "//") == 0) {
    char qb[64];
    if (b->d == 0.0) return arith_err("division by zero", NULL);
    fmt_double_n(a->d / b->d, 16, qb);
    av_text_in(r, trunc_text(qb));
    return 1;
  }
  if (strcmp(f, "mod") == 0) {
    if (b->d == 0.0) return arith_err("division by zero", NULL);
    av_make(r, fmod(a->d, b->d));
    if (r->d != 0.0) {
      int mneg = r->d < 0, bneg = b->d < 0;
      if (mneg != bneg) av_make(r, r->d + b->d);
    }
    return 1;
  }
  if (strcmp(f, "rem") == 0) {
    if (b->d == 0.0) return arith_err("division by zero", NULL);
    av_make(r, fmod(a->d, b->d));
    return 1;
  }
  if (strcmp(f, "**") == 0) { av_make(r, pow(a->d, b->d)); return 1; }
  if (strcmp(f, "min") == 0) { *r = a->d < b->d ? *a : *b; return 1; }
  if (strcmp(f, "max") == 0) { *r = a->d > b->d ? *a : *b; return 1; }
  {
    Buf bb;
    bb.s = NULL; bb.n = bb.cap = 0;
    bput(&bb, f);
    bput(&bb, "/2");
    arith_err("unknown function", bstr(&bb));
    free(bb.s);
  }
  return 0;
}

static int arith_eval(Term *t, AV *r)
{
  AV a, b;
  t = deref(t);
  if (t->tag == T_VAR) return arith_err("unbound variable", NULL);
  if (t->tag == T_NUM) {
    r->lit = t->u.n.txt;
    r->src = t;
    r->d = num_val(t->u.n.txt);
    r->buf[0] = '\0';
    return 1;
  }
  if (t->tag != T_CMP) {
    Buf sb;
    const char *nm;
    sb.s = NULL; sb.n = sb.cap = 0;
    nm = atext(t, &sb);
    if (strcmp(nm, "pi") == 0) av_text_in(r, "3.14159265358979");
    else if (strcmp(nm, "e") == 0) av_text_in(r, "2.71828182845905");
    else {
      arith_err("not evaluable:", nm);
      free(sb.s);
      return 0;
    }
    free(sb.s);
    return 1;
  }
  if (t->u.c.ar == 1) {
    if (!arith_eval(t->u.c.args[0], &a)) return 0;
    return arith_fn1(t->u.c.fn, &a, r);
  }
  if (t->u.c.ar == 2) {
    if (!arith_eval(t->u.c.args[0], &a)) return 0;
    if (!arith_eval(t->u.c.args[1], &b)) return 0;
    return arith_fn2(t->u.c.fn, &a, &b, r);
  }
  charout("prolog: arithmetic: not evaluable: ");
  charout(write_term(t, 1));
  charout("\n");
  return 0;
}

static Term *av_term(AV *r)
{
  if (r->src != NULL) return r->src;
  return mk_from_text(av_text(r));
}

/* ---- the engine ---- */
enum { M_FIRST, M_FINDALL, M_INTERACT };
enum { F_CUT, F_DISJ, F_ITE, F_ITE0, F_NEG, F_USER, F_FA };

typedef struct FAcc {
  Arena ar;
  Term **res;
  long n, cap;
  Term **nmap;              /* copies of the variables made inside the findall goal */
  long nmapn, nmapcap;
} FAcc;

typedef struct Frame {
  int kind;
  int savedmode;
  long tmark;
  AMark hmark;
  Goal *rest;
  Term *a, *b;
  Clause **v;
  int n, i;
  int *ipos;               /* indexed call: original positions of v[] */
  long *pre;               /* index: prefix sums of nhead */
  int last, norig;         /* index: last position entered, clauses in the snapshot */
  long barrier;
  FAcc *fa, *savedfa;
  Term *tmpl, *savedtmpl;
  long fapos;
} Frame;

static Frame *g_stk;
static long g_sp, g_stkcap;
static int g_mode = M_FIRST;
static long g_cutsig, g_callcount;
static FAcc *g_fa;
static Term *g_fatmpl;
static long g_fapos;

/* ---- elided frames ---------------------------------------------------------
   A user call that has entered its last candidate clause is no choice point any
   more, but its frame used to stay on the stack until the call failed or the whole
   query succeeded, so a deterministic recursion of N steps kept N frames.  Such a
   frame is now taken off the stack at once and replaced by a small record of what
   failing back through it would still do:
     - clear g_cutsig when the cut signal is for this call (or, after an eager cut
       of the call, always: that is the F_CUT frame plus the call frame);
     - advance the renaming counter g_rvc by the clauses that would still have
       been tried (and failed) or that an indexed call skipped;
     - undo the trail and release the heap to the frame's marks.
   The records between frame k-1 and frame k form the group of depth k (records of
   one run only, from g_tbase on).  A record whose body has been completely entered
   (the current continuation is its continuation) can no longer be the target of a
   cut, so it is dropped and its remaining effect is folded into the record below:
   (A, B, C) means "g_rvc += A if g_cutsig == 0, else g_rvc += B; then clear
   g_cutsig if C".  The lowest record of a group keeps its marks and becomes the
   group's base record (b = 0).  A tail-recursive loop keeps two records in all.
   A cut whose target lies below only plain call frames and records (no other cut,
   disjunction, if-then-else, negation or findall) is done at once: those frames
   and records are dropped and the target becomes a record with c = 1, which is
   what failing back into the F_CUT frame of the original engine would have done.
   Otherwise the F_CUT frame is pushed as before.  Answers, the order of side
   effects and the renaming counters (variable names) are exactly as before; the
   collector needs the bounded stack. */
typedef struct {
  long depth;              /* sits between g_stk[depth - 1] and g_stk[depth] */
  long b;                  /* barrier (call number) of the elided call; 0 = base record */
  long d;                  /* g_rvc advance when the call is exhausted (indexed call) */
  int c;                   /* the call was cut: failing through it clears g_cutsig */
  long acca, accb;         /* folded effect of dropped records above this one */
  int accc;
  Goal *rest;              /* the call's continuation */
  long tmark;              /* the frame's marks */
  AMark hmark;
} Tomb;

static Term **g_fr;             /* try_clause's variable frame */
static int g_frcap;

static Tomb *g_tomb;
static long g_tombn, g_tombcap, g_tbase;
static int g_elide = 1;
static unsigned long g_nelided;

/* ---- garbage collector controls (the collector itself is in p5_gc.c) ---- */
#ifndef GC_MIN
#define GC_MIN 4194304L
#endif
static int g_gc_on = 1, g_gc_req;
static long g_gc_stress, g_gc_tick;
static long g_gc_next = GC_MIN, g_gc_znext = GC_MIN;
static void gc_collect(Goal **gp);

#define HEAP_TOP() (g_heap.n ? g_heap.b[g_heap.cur].base + (long)g_heap.b[g_heap.cur].used : 0L)
#define GC_DUE() (g_gc_req || (g_gc_on && (g_gc_stress > 0 ? ++g_gc_tick >= g_gc_stress : \
                  (HEAP_TOP() > g_gc_next || g_zbytes > (unsigned long)g_gc_znext))))
/* drop the records whose body is complete (tomb_drop), cheap test first */
#define TOMB_DROP(cont) do { if (g_tombn > g_tbase + 1 && g_tomb[g_tombn - 1].rest == (cont)) \
                               tomb_drop(cont); } while (0)

/* C variables of the callers of run() that hold heap data while a collection can run */
enum { GR_TERM, GR_GOAL, GR_AMARK, GR_TMARK };
typedef struct { int kind; void *p; } GRoot;
static GRoot *g_roots;
static int g_nroots, g_rootcap;

static void groot(int kind, void *p)
{
  if (g_nroots >= g_rootcap) {
    g_rootcap = g_rootcap ? g_rootcap * 2 : 16;
    g_roots = (GRoot *)xrealloc(g_roots, sizeof(GRoot) * (size_t)g_rootcap);
  }
  g_roots[g_nroots].kind = kind;
  g_roots[g_nroots].p = p;
  g_nroots++;
}

static void tomb_push(long depth, long b, long d, int c, Goal *rest)
{
  Tomb *t;
  if (g_tombn >= g_tombcap) {
    g_tombcap = g_tombcap ? g_tombcap * 2 : 256;
    g_tomb = (Tomb *)xrealloc(g_tomb, sizeof(Tomb) * (size_t)g_tombcap);
  }
  t = &g_tomb[g_tombn++];
  t->depth = depth;
  t->b = b;
  t->d = d;
  t->c = c;
  t->acca = t->accb = 0;
  t->accc = 0;
  t->rest = rest;
  t->tmark = 0;
  t->hmark.blk = 0;
  t->hmark.off = 0;
}

/* take the top frame (a call in its last clause, or a cut one) off the stack; dskip:
   renaming numbers the remaining clauses would use up when tried (see run) */
static void elide_top(int cut, long dskip)
{
  Frame *fp = &g_stk[g_sp - 1];
  long d = dskip;
  if (!cut && fp->ipos != NULL) d = fp->pre[fp->norig] - fp->pre[fp->last + 1];
  g_sp--;
  tomb_push(g_sp, fp->barrier, d, cut, fp->rest);
  g_tomb[g_tombn - 1].tmark = fp->tmark;
  g_tomb[g_tombn - 1].hmark = fp->hmark;
  g_nelided++;
}

/* drop the records whose body has been entered completely (continuation == cont) */
static void tomb_drop(Goal *cont)
{
  while (g_tombn > g_tbase) {
    Tomb *t = &g_tomb[g_tombn - 1], *u = t - 1;
    long ea, eb;
    int ec, bottom = g_tombn - 1 == g_tbase || u->depth != t->depth;
    if (t->b == 0 || t->rest != cont || t->depth != g_sp || g_cutsig == t->b) return;
    if (!bottom && !t->c && !t->d && !t->accc && !t->acca && !t->accb) {   /* no effect */
      g_tombn--;
      continue;
    }
    /* E = acc(t) followed by t itself (g_cutsig can no longer name t) */
    ea = t->acca + (t->c ? 0 : t->d);
    eb = t->accb + (t->accc ? (t->c ? 0 : t->d) : 0);
    ec = t->accc || t->c;
    if (bottom) {            /* becomes the group's base record: marks and E only */
      t->b = 0;
      t->c = 0;
      t->d = 0;
      t->rest = NULL;
      t->acca = ea;
      t->accb = eb;
      t->accc = ec;
      return;
    }
    /* u.acc = E followed by u.acc */
    eb = eb + (ec ? u->acca : u->accb);
    u->acca = ea + u->acca;
    u->accb = eb;
    u->accc = ec || u->accc;
    g_tombn--;
  }
}

/* failing (v = 0) or succeeding (v = 1) through the group above the top frame */
static void tomb_unwind(int v)
{
  while (g_tombn > g_tbase && g_tomb[g_tombn - 1].depth == g_sp) {
    Tomb *t = &g_tomb[--g_tombn];
    if (v) continue;
    if (g_cutsig == 0) g_rvc += t->acca;
    else g_rvc += t->accb;
    if (t->accc) g_cutsig = 0;
    if (t->b == 0) {
      ;
    } else if (t->c || g_cutsig == t->b) {
      g_cutsig = 0;
    } else if (g_cutsig == 0) {
      g_rvc += t->d;
    }
    trail_undo(t->tmark);
    arelease(&g_heap, t->hmark);
  }
}

/* the cut of call b (see above); 0 when an F_CUT frame has to be pushed instead */
static int cut_now(long b, long base)
{
  long k = g_sp - 1, ti = g_tombn - 1;
  for (;;) {
    while (ti >= g_tbase && g_tomb[ti].depth == k + 1) {
      Tomb *t = &g_tomb[ti];
      if (t->b == b && !t->accc) {
        g_tombn = ti + 1;
        g_sp = k + 1;
        t->c = 1;
        t->d = 0;
        t->acca = t->accb = 0;
        return 1;
      }
      if (t->c || t->accc) return 0;
      ti--;
    }
    if (k < base || g_stk[k].kind != F_USER) return 0;
    if (g_stk[k].barrier == b) {
      g_tombn = ti + 1;
      g_sp = k + 1;
      elide_top(1, 0);
      return 1;
    }
    k--;
  }
}

static int bi_exec(int id, Term *goal);
static int bi_sort_list(Term *goal, int dedupe);
#ifndef NSORT_MIN
#define NSORT_MIN 32
#endif

/* length of a proper list, -1 if the term is not one */
static long proper_list_len(Term *t)
{
  long n = 0;
  t = deref(t);
  while (t->tag == T_CMP && t->u.c.fn == S_DOT && t->u.c.ar == 2) {
    n++;
    t = deref(t->u.c.args[1]);
  }
  return t == S_NIL->pt ? n : -1;
}
static int on_solution(void);
#define BI_FINDALL_ID 1

static Goal *new_goal(Term *t, Goal *next)
{
  Goal *g = (Goal *)aalloc_k(&g_heap, sizeof(Goal), HK_GOAL);
  g->t = t;
  g->next = next;
  return g;
}

/* can a clause whose first head argument is the template t match the first argument a
   (dereferenced, not a variable)?  The same test unify_head does first. */
static int may_match0(Term *t, Term *a)
{
  if (t->tag == T_TV) return 1;
  if (t->tag == T_CMP || a->tag == T_CMP)
    return t->tag == T_CMP && a->tag == T_CMP && t->u.c.fn == a->u.c.fn && t->u.c.ar == a->u.c.ar;
  if (t == a) return 1;
  return t->tag == T_NUM && a->tag == T_NUM && num_eq(t, a);
}

/* 0 when head unification of clause c with the goal certainly fails: an argument
   bound in the goal has another principal functor than the clause head's */
static int may_match(Clause *c, Term *goal)
{
  int k;
  Term *a;
  for (k = 0; k < goal->u.c.ar; k++) {
    if (c->head->u.c.args[k]->tag == T_TV) continue;   /* (no dereferencing for nothing) */
    a = deref(goal->u.c.args[k]);
    if (a->tag != T_VAR && !may_match0(c->head->u.c.args[k], a)) return 0;
  }
  return 1;
}

static Frame *push_frame(int kind)
{
  Frame *f;
  if (g_sp >= g_stkcap) {
    g_stkcap = g_stkcap ? g_stkcap * 2 : 1024;
    g_stk = (Frame *)xrealloc(g_stk, sizeof(Frame) * (size_t)g_stkcap);
  }
  f = &g_stk[g_sp++];
  f->kind = kind;
  f->rest = NULL;
  f->a = f->b = NULL;
  f->v = NULL;
  f->ipos = NULL;
  f->fa = f->savedfa = NULL;
  f->tmpl = f->savedtmpl = NULL;
  f->tmark = trail_mark();
  f->hmark = amark(&g_heap);
  f->savedmode = g_mode;
  return f;
}

static void arena_free(Arena *a)
{
  int i;
  for (i = 0; i < a->n; i++) free(a->b[i].p);
  free(a->b);
  a->b = NULL;
  a->n = a->ncap = a->cur = 0;
}

/* copy a solution out of the heap: numbers and new variables are duplicated */
static Term *fa_out(FAcc *fa, Term *t)
{
  Term *first = NULL, **hole = &first, *r;
  for (;;) {
    t = deref(t);
    if (t->tag == T_VAR) {
      long i;
      Term *c;
      if (t->u.v.born < g_fapos) { *hole = t; return first; }
      /* known by its name, as in the CREXX version where a term is its text (a
         variable of an earlier answer may have lived at the same address) */
      for (i = 0; i < fa->nmapn; i++) {
        c = fa->nmap[i];
        if (c->u.v.ctr == t->u.v.ctr && c->u.v.sep == t->u.v.sep &&
            strcmp(c->u.v.base, t->u.v.base) == 0) {
          *hole = c;
          return first;
        }
      }
      c = (Term *)aalloc(&fa->ar, sizeof(Term));
      c->tag = T_VAR;
      c->gnd = 0;
      c->u.v.ref = NULL;
      c->u.v.base = astrdup(&fa->ar, t->u.v.base);
      c->u.v.sep = t->u.v.sep;
      c->u.v.ctr = t->u.v.ctr;
      c->u.v.born = -1;
      if (fa->nmapn >= fa->nmapcap) {
        fa->nmapcap = fa->nmapcap ? fa->nmapcap * 2 : 16;
        fa->nmap = (Term **)xrealloc(fa->nmap, sizeof(Term *) * (size_t)fa->nmapcap);
      }
      fa->nmap[fa->nmapn] = c;
      fa->nmapn++;
      *hole = c;
      return first;
    }
    if (t->tag == T_NUM) { *hole = mk_numa(&fa->ar, t->u.n.txt); return first; }
    if (t->tag != T_CMP || t->u.c.ar == 0) {
      if (t->tag == T_CMP) {
        *hole = mk_cmpa(&fa->ar, t->u.c.fn, 0);
      } else {
        *hole = t;
      }
      return first;
    }
    {
      int i, n = t->u.c.ar;
      r = mk_cmpa(&fa->ar, t->u.c.fn, n);
      for (i = 0; i < n - 1; i++) r->u.c.args[i] = fa_out(fa, t->u.c.args[i]);
      *hole = r;
      hole = &r->u.c.args[n - 1];
      t = t->u.c.args[n - 1];
    }
  }
}

typedef struct { Term **o, **n; long cnt, cap; } InMap;

static Term *fa_in(InMap *m, Term *t)
{
  Term *first = NULL, **hole = &first, *r;
  for (;;) {
    if (t->tag == T_VAR && t->u.v.born == -1) {
      long i;
      Term *c;
      for (i = 0; i < m->cnt; i++)
        if (m->o[i] == t) { *hole = m->n[i]; return first; }
      c = mk_var(astrdup(&g_heap, t->u.v.base), t->u.v.sep, t->u.v.ctr);
      if (m->cnt >= m->cap) {
        m->cap = m->cap ? m->cap * 2 : 16;
        m->o = (Term **)xrealloc(m->o, sizeof(Term *) * (size_t)m->cap);
        m->n = (Term **)xrealloc(m->n, sizeof(Term *) * (size_t)m->cap);
      }
      m->o[m->cnt] = t;
      m->n[m->cnt] = c;
      m->cnt++;
      *hole = c;
      return first;
    }
    if (t->tag == T_NUM) { *hole = mk_num(t->u.n.txt); return first; }
    if (t->tag != T_CMP || t->u.c.ar == 0) {
      if (t->tag == T_CMP) *hole = mk_cmp(t->u.c.fn, 0);
      else *hole = t;
      return first;
    }
    {
      int i, n = t->u.c.ar;
      r = mk_cmp(t->u.c.fn, n);
      for (i = 0; i < n - 1; i++) r->u.c.args[i] = fa_in(m, t->u.c.args[i]);
      *hole = r;
      hole = &r->u.c.args[n - 1];
      t = t->u.c.args[n - 1];
    }
  }
}

static Term *fa_list(FAcc *fa)
{
  InMap m;
  Term *r = S_NIL->pt;
  long i;
  m.o = m.n = NULL;
  m.cnt = m.cap = 0;
  for (i = fa->n - 1; i >= 0; i--) {
    Term *e = fa_in(&m, fa->res[i]);
    r = mk_cons(e, r);
  }
  free(m.o);
  free(m.n);
  return r;
}

static void fa_free(FAcc *fa)
{
  arena_free(&fa->ar);
  free(fa->res);
  free(fa->nmap);
  free(fa);
}

static int run(Goal *goals)
{
  long base = g_sp, otbase = g_tbase;
  int v = 0, ar, ar2, i, k, ok, lastcl;
  long dskip;
  Term *goal, *g0, *left, **fr;
  Goal *rest;
  Sym *fn;
  Frame *fp;
  Clause *cl;
  Pred *pr;
  long cbase;

  g_tbase = g_tombn;
eval:
  /* the safe point: everything live is reachable from goals, the frames, the
     elided-frame records and the registered roots */
  TOMB_DROP(goals);
  if (GC_DUE()) gc_collect(&goals);
  if (goals == NULL) {
    v = on_solution();
    goto ret;
  }
  goal = deref(goals->t);
  rest = goals->next;
  if (goal->tag == T_VAR) {
    say("prolog: instantiation error (unbound goal)");
    v = 0;
    goto ret;
  }
  if (goal->tag == T_CUT) {
    if (!g_elide || !cut_now(goal->u.x.idx, base)) {
      fp = push_frame(F_CUT);
      fp->barrier = goal->u.x.idx;
    }
    goals = rest;
    goto eval;
  }
  if (goal->tag == T_CMP) { fn = goal->u.c.fn; ar = goal->u.c.ar; }
  else fn = functor_of(goal, &ar);

  if (fn == S_COMMA && ar == 2) {
    goals = new_goal(goal->u.c.args[0], new_goal(goal->u.c.args[1], rest));
    goto eval;
  }
  if (fn == S_TRUE && ar == 0) { goals = rest; goto eval; }
  if ((fn == S_FAIL || fn == S_FALSE) && ar == 0) { v = 0; goto ret; }
  if (fn == S_BANG && ar == 0) { goals = rest; goto eval; }
  if (fn == S_SEMI && ar == 2) {
    left = goal->u.c.args[0];
    if (left->tag == T_CMP && left->u.c.fn == S_ARROW && left->u.c.ar == 2) {
      fp = push_frame(F_ITE);
      fp->a = left->u.c.args[1];
      fp->b = goal->u.c.args[1];
      fp->rest = rest;
      g_mode = M_FIRST;
      goals = new_goal(left->u.c.args[0], NULL);
      goto eval;
    }
    fp = push_frame(F_DISJ);
    fp->a = goal->u.c.args[1];
    fp->rest = rest;
    goals = new_goal(left, rest);
    goto eval;
  }
  if (fn == S_ARROW && ar == 2) {
    fp = push_frame(F_ITE0);
    fp->a = goal->u.c.args[1];
    fp->rest = rest;
    g_mode = M_FIRST;
    goals = new_goal(goal->u.c.args[0], NULL);
    goto eval;
  }
  if ((fn == S_NOTP || fn == S_NOT) && ar == 1) {
    fp = push_frame(F_NEG);
    fp->rest = rest;
    g_mode = M_FIRST;
    goals = new_goal(goal->u.c.args[0], NULL);
    goto eval;
  }
  if (fn == S_CALL && ar >= 1) {
    g0 = deref(goal->u.c.args[0]);
    if (ar == 1) {
      goals = new_goal(g0, rest);
    } else {
      Sym *f2 = functor_of(g0, &ar2);
      Term *ng = mk_cmp(f2, ar2 + ar - 1);
      for (i = 0; i < ar2; i++) ng->u.c.args[i] = g0->u.c.args[i];
      for (i = 1; i < ar; i++) ng->u.c.args[ar2 + i - 1] = goal->u.c.args[i];
      goals = new_goal(ng, rest);
    }
    goto eval;
  }
  if (ar < 4 && fn->bi[ar]) {
    if (fn->bi[ar] == BI_FINDALL_ID) {
      FAcc *fa = (FAcc *)xmalloc(sizeof(FAcc));
      memset(fa, 0, sizeof(FAcc));
      fp = push_frame(F_FA);
      fp->fa = fa;
      fp->savedfa = g_fa;
      fp->savedtmpl = g_fatmpl;
      fp->fapos = g_fapos;
      fp->b = goal->u.c.args[2];
      fp->rest = rest;
      g_mode = M_FINDALL;
      g_fa = fa;
      g_fatmpl = goal->u.c.args[0];
      g_fapos = apos(&g_heap);
      goals = new_goal(goal->u.c.args[1], NULL);
      goto eval;
    }
    groot(GR_GOAL, &rest);      /* consult/1 runs directives: a collection can happen */
    ok = bi_exec(fn->bi[ar], goal);
    g_nroots--;
    if (ok) goals = rest;
    else { v = 0; goto ret; }
    goto eval;
  }
  /* msort/2 and sort/2 of a long proper list: merge sort in C instead of the insertion sort
     of the library (quadratic).  Short lists keep the library path so that the variable
     renaming counters stay exactly as before. */
  if (ar == 2 && (fn == S_MSORT || fn == S_SORT)) {
    pr = pred_find(fn, ar, 0);
    if (pr != NULL && pr->n > 0 && pr->v[0]->id <= g_bootmax && pr->v[pr->n - 1]->id <= g_bootmax &&
        proper_list_len(goal->u.c.args[0]) >= NSORT_MIN) {
      if (bi_sort_list(goal, fn == S_SORT)) goals = rest;
      else { v = 0; goto ret; }
      goto eval;
    }
  }
  /* a user predicate */
  pr = pred_find(fn, ar, 0);
  if (pr == NULL || pr->n == 0) {
    if (pr == NULL || !pr->listed) {
      char num[24];
      charout("prolog: unknown procedure ");
      charout(fn->text);
      sprintf(num, "/%d", ar);
      say(num);
    }
    v = 0;
    goto ret;
  }
  TOMB_DROP(rest);
  fp = push_frame(F_USER);
  fp->a = goal;
  fp->rest = rest;
  fp->v = pr->v;
  fp->n = pr->n;
  fp->i = 0;
  fp->ipos = NULL;
  fp->barrier = ++g_callcount;
  if (ar >= 1 && pr->n >= IDX_MIN) {
    IBkt *bk = NULL;
    PIdx *ix = idx_for_call(pr, deref(goal->u.c.args[0]), &bk);
    if (ix != NULL) {
      fp->v = bk->c;
      fp->n = bk->n;
      fp->ipos = bk->pos;
      fp->pre = ix->pre;
      fp->last = -1;
      fp->norig = pr->n;
    }
  }

try_clause:
  fp = &g_stk[g_sp - 1];
  if (fp->i >= fp->n) {
    if (fp->ipos != NULL) g_rvc += fp->pre[fp->norig] - fp->pre[fp->last + 1];
    g_sp--;
    v = 0;
    goto ret;
  }
  fp->tmark = trail_mark();
  fp->hmark = amark(&g_heap);
  cl = fp->v[fp->i];
  if (fp->ipos != NULL) {   /* clauses skipped by the index still use up renaming numbers */
    g_rvc += fp->pre[fp->ipos[fp->i]] - fp->pre[fp->last + 1];
    fp->last = fp->ipos[fp->i];
  }
  /* no alternative after this clause: it is the last one, or the remaining ones (a few,
     not indexed) cannot match the goal's arguments -- trying them would only use up
     their renaming numbers, dskip */
  lastcl = 0;
  dskip = 0;
  if (g_elide) {
    if (fp->i == fp->n - 1) lastcl = 1;
    else if (fp->ipos == NULL && fp->n - fp->i <= 8 && fp->a->tag == T_CMP && fp->a->u.c.ar > 0) {
      for (k = fp->i + 1; k < fp->n; k++) {
        if (may_match(fp->v[k], fp->a)) break;
        dskip += fp->v[k]->nhead;
      }
      lastcl = k == fp->n;
    }
  }
  /* the clause's variables: only needed while the head and body are instantiated */
  if (cl->nvars + 1 > g_frcap) {
    g_frcap = cl->nvars + 64;
    g_fr = (Term **)xrealloc(g_fr, sizeof(Term *) * (size_t)g_frcap);
  }
  fr = g_fr;
  for (k = 0; k < cl->nvars; k++) fr[k] = NULL;
  cbase = g_rvc;
  g_rvc += cl->nhead;
  ok = unify_head(cl->head, fp->a, fr, cl, cbase);
  if (ok) {
    Goal *g = fp->rest;
    long barrier = fp->barrier;
    g_rvc += cl->nvars - cl->nhead;
    for (k = cl->nbody - 1; k >= 0; k--)
      g = new_goal(inst_goal(cl->body[k], fr, cl, cbase, barrier), g);
    goals = g;
    if (lastcl) elide_top(0, dskip);
    goto eval;
  }
  trail_undo(fp->tmark);
  arelease(&g_heap, fp->hmark);
  if (g_cutsig != 0) {
    if (g_cutsig == fp->barrier) g_cutsig = 0;
    g_sp--;
    v = 0;
    goto ret;
  }
  fp->i++;
  goto try_clause;

ret:
  if (g_tombn > g_tbase && g_tomb[g_tombn - 1].depth == g_sp) tomb_unwind(v);
  if (g_sp == base) {
    g_tbase = otbase;
    return v;
  }
  fp = &g_stk[g_sp - 1];
  switch (fp->kind) {
  case F_CUT: {
    long b = fp->barrier;
    g_sp--;
    if (!v) g_cutsig = b;
    goto ret;
  }
  case F_DISJ:
    g_sp--;
    if (v) goto ret;
    trail_undo(fp->tmark);
    arelease(&g_heap, fp->hmark);
    goals = new_goal(fp->a, fp->rest);
    goto eval;
  case F_ITE:
    g_sp--;
    g_mode = fp->savedmode;
    if (v) {
      goals = new_goal(fp->a, fp->rest);
      goto eval;
    }
    trail_undo(fp->tmark);
    arelease(&g_heap, fp->hmark);
    goals = new_goal(fp->b, fp->rest);
    goto eval;
  case F_ITE0:
    g_sp--;
    g_mode = fp->savedmode;
    if (v) {
      goals = new_goal(fp->a, fp->rest);
      goto eval;
    }
    trail_undo(fp->tmark);
    arelease(&g_heap, fp->hmark);
    v = 0;
    goto ret;
  case F_NEG:
    g_sp--;
    g_mode = fp->savedmode;
    trail_undo(fp->tmark);
    arelease(&g_heap, fp->hmark);
    if (v) { v = 0; goto ret; }
    goals = fp->rest;
    goto eval;
  case F_USER:
    if (v) { g_sp--; goto ret; }
    trail_undo(fp->tmark);
    arelease(&g_heap, fp->hmark);
    if (g_cutsig != 0) {
      if (g_cutsig == fp->barrier) g_cutsig = 0;
      g_sp--;
      v = 0;
      goto ret;
    }
    fp->i++;
    goto try_clause;
  default: {                          /* F_FA */
    FAcc *fa = fp->fa;
    Term *lst, *res;
    Goal *rs = fp->rest;
    g_sp--;
    g_mode = fp->savedmode;
    g_fa = fp->savedfa;
    g_fatmpl = fp->savedtmpl;
    g_fapos = fp->fapos;
    res = fp->b;
    trail_undo(fp->tmark);
    arelease(&g_heap, fp->hmark);
    lst = fa_list(fa);
    fa_free(fa);
    if (unify(res, lst)) {
      goals = rs;
      goto eval;
    }
    v = 0;
    goto ret;
  }
  }
}

/* a solution of the goal list: what happens depends on the mode */
static int interact_on_solution(void);

static int on_solution(void)
{
  switch (g_mode) {
  case M_FIRST: return 1;
  case M_FINDALL: {
    FAcc *fa = g_fa;
    if (fa->n >= fa->cap) {
      fa->cap = fa->cap ? fa->cap * 2 : 16;
      fa->res = (Term **)xrealloc(fa->res, sizeof(Term *) * (size_t)fa->cap);
    }
    fa->res[fa->n++] = fa_out(fa, g_fatmpl);
    return 0;
  }
  default: return interact_on_solution();
  }
}

static int solve_once(Term *goal)
{
  int saved = g_mode, found;
  g_mode = M_FIRST;
  found = run(new_goal(goal, NULL));
  g_mode = saved;
  return found;
}
